/**
 * This program and the accompanying materials are made available under the terms of the
 * Eclipse Public License v2.0 which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v20.html
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Copyright Contributors to the Zowe Project.
 *
 */

#include "worker.hpp"
#include "rpc_server.hpp"
#include "../zlogger.hpp"
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <system_error>
#include <unistd.h>

using std::string;

namespace
{

// Force-detached threads that have not yet unwound, process-wide.
std::atomic<size_t> &live_detached_workers()
{
  static std::atomic<size_t> counter{0};
  return counter;
}

// Exit code used when the pool gives up because too many threads are wedged.
const int kDetachedWorkerLimitExitCode = 3;

const char *worker_state_to_string(WorkerState state)
{
  switch (state)
  {
  case WorkerState::Starting:
    return "Starting";
  case WorkerState::Idle:
    return "Idle";
  case WorkerState::Running:
    return "Running";
  case WorkerState::Stopping:
    return "Stopping";
  case WorkerState::Faulted:
    return "Faulted";
  case WorkerState::Exited:
    return "Exited";
  default:
    return "Unknown";
  }
}

} // namespace

// Worker implementation
Worker::Worker(int worker_id)
    : id(worker_id)
{
  LOG_DEBUG("Worker %d state -> %s (constructor)", id, worker_state_to_string(WorkerState::Starting));
}

Worker::~Worker()
{
  stop();
}

void Worker::start()
{
  stop_requested.store(false, std::memory_order_release);
  state.store(WorkerState::Starting, std::memory_order_release);
  update_heartbeat();
  LOG_DEBUG("Worker %d state -> %s (start invoked)", id, worker_state_to_string(WorkerState::Starting));

  auto self = shared_from_this();

  worker_thread = std::thread(&Worker::worker_loop, self);
  state.store(WorkerState::Idle, std::memory_order_release);
  update_heartbeat();
  LOG_DEBUG("Worker %d state -> %s (worker thread started)", id, worker_state_to_string(WorkerState::Idle));
  LOG_DEBUG("Worker %d started", id);
}

void Worker::stop()
{
  WorkerState current_state = state.load(std::memory_order_acquire);
  if (current_state == WorkerState::Exited)
    return;

  stop_requested.store(true, std::memory_order_release);

  if (current_state != WorkerState::Faulted)
  {
    state.store(WorkerState::Stopping, std::memory_order_release);
    LOG_DEBUG("Worker %d state -> %s (stop requested)", id, worker_state_to_string(WorkerState::Stopping));
  }

  queue_condition.notify_all();

  if (worker_thread.joinable())
  {
    std::unique_lock<std::mutex> lock(exit_mutex);
    bool exited = exit_condition.wait_for(lock, std::chrono::seconds(5), [this] {
      auto s = state.load(std::memory_order_acquire);
      return s == WorkerState::Exited || s == WorkerState::Faulted;
    });
    lock.unlock();

    if (!exited)
    {
      LOG_WARN("Worker %d did not exit within stop timeout; detaching", id);
      worker_thread.detach();
      state.store(WorkerState::Faulted, std::memory_order_release);
      return;
    }

    if (worker_thread.joinable())
      worker_thread.join();
  }

  if (state.load(std::memory_order_acquire) != WorkerState::Faulted)
  {
    state.store(WorkerState::Exited, std::memory_order_release);
    LOG_DEBUG("Worker %d state -> %s (stop complete)", id, worker_state_to_string(WorkerState::Exited));
  }

  if (state.load(std::memory_order_acquire) != WorkerState::Faulted)
    LOG_DEBUG("Worker %d stopped", id);
  else
    LOG_DEBUG("Worker %d stop requested while faulted/detached", id);
}

void Worker::add_request(const RequestMetadata &request)
{
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    request_queue.push(request);
  }
  queue_condition.notify_one();
}

void Worker::worker_loop()
{
  try
  {
    while (true)
    {
      RequestMetadata request_metadata;

      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        state.store(WorkerState::Idle, std::memory_order_release);
        update_heartbeat();
        queue_condition.wait(lock, [this]
                             { return stop_requested.load(std::memory_order_acquire) || !request_queue.empty(); });

        if (stop_requested.load(std::memory_order_acquire))
        {
          state.store(WorkerState::Stopping, std::memory_order_release);
          update_heartbeat();
          LOG_DEBUG("Worker %d state -> %s (stop signaled)", id, worker_state_to_string(WorkerState::Stopping));
          break;
        }

        if (request_queue.empty())
        {
          update_heartbeat();
          continue;
        }

        request_metadata = request_queue.front();
        request_queue.pop();
        state.store(WorkerState::Running, std::memory_order_release);
        update_heartbeat();
      }

      // Track current request for potential recovery
      {
        std::lock_guard<std::mutex> lock(current_request_mutex);
        current_request_data = request_metadata.data;
      }

      process_request(request_metadata.data);
      update_heartbeat();

      // Clear current request after successful processing
      {
        std::lock_guard<std::mutex> lock(current_request_mutex);
        current_request_data.clear();
      }
    }

    state.store(WorkerState::Exited, std::memory_order_release);
    update_heartbeat();
    LOG_DEBUG("Worker %d state -> %s (worker loop exit)", id, worker_state_to_string(WorkerState::Exited));
  }
  catch (const std::exception &e)
  {
    state.store(WorkerState::Faulted, std::memory_order_release);
    update_heartbeat();
    LOG_DEBUG("Worker %d state -> %s (exception)", id, worker_state_to_string(WorkerState::Faulted));
    stop_requested.store(true, std::memory_order_release);
    LOG_ERROR("Worker %d encountered fatal error: %s", id, e.what());
  }

  if (detached.exchange(false))
  {
    // The stuck operation finally returned, so this thread is no longer a leak.
    const size_t remaining = live_detached_workers().fetch_sub(1) - 1;
    LOG_DEBUG("Worker %d detached thread unwound; %zu detached worker thread(s) still live", id, remaining);
  }

  {
    std::lock_guard<std::mutex> lock(exit_mutex);
  }
  exit_condition.notify_all();
}

bool Worker::is_ready() const
{
  WorkerState current = state.load(std::memory_order_acquire);
  return current == WorkerState::Idle || current == WorkerState::Running;
}

bool Worker::is_running() const
{
  return state.load(std::memory_order_acquire) == WorkerState::Running;
}

bool Worker::has_fault() const
{
  return state.load(std::memory_order_acquire) == WorkerState::Faulted;
}

bool Worker::is_stop_requested() const
{
  WorkerState current = state.load(std::memory_order_acquire);
  if (current == WorkerState::Stopping || current == WorkerState::Exited || current == WorkerState::Faulted)
    return true;

  return stop_requested.load(std::memory_order_acquire);
}

WorkerState Worker::get_state() const
{
  return state.load(std::memory_order_acquire);
}

void Worker::process_request(const string &data)
{
  // Delegate JSON-RPC processing to the RpcServer singleton. The heartbeat callback
  // lets streaming command handlers (e.g. large file uploads) touch this worker's
  // heartbeat per chunk, so the supervisor's request-timeout watchdog sees ongoing
  // progress instead of only the start/end of the request.
  RpcServer &server = RpcServer::get_instance();
  server.process_request(data, [this]
                          { update_heartbeat(); });
}

void Worker::update_heartbeat()
{
  last_heartbeat_ms.store(steady_clock_now_ms(), std::memory_order_release);
}

std::chrono::steady_clock::time_point Worker::get_last_heartbeat() const
{
  const auto stored_ms = last_heartbeat_ms.load(std::memory_order_acquire);
  const auto duration_ms = std::chrono::milliseconds(stored_ms);
  const auto steady_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration_ms);
  return std::chrono::steady_clock::time_point(steady_duration);
}

void Worker::force_detach()
{
  stop_requested.store(true, std::memory_order_release);
  state.store(WorkerState::Faulted, std::memory_order_release);
  queue_condition.notify_all();
  LOG_WARN(worker_thread.joinable() ? "Worker %d forcibly detached due to heartbeat timeout" : "Worker %d marked faulted due to heartbeat timeout (thread not joinable)", id);
  if (worker_thread.joinable())
  {
    // The thread cannot be cancelled safely while it sits in a system service, so
    // account for it as a live leak until worker_loop() unwinds on its own.
    detached.store(true);
    const size_t live = live_detached_workers().fetch_add(1) + 1;
    worker_thread.detach();
    LOG_WARN("Worker %d thread detached and still running; %zu detached worker thread(s) now live", id, live);
  }
  update_heartbeat();
}

size_t Worker::live_detached_count()
{
  return live_detached_workers().load();
}

std::vector<RequestMetadata> Worker::drain_pending_requests()
{
  std::vector<RequestMetadata> drained_requests;

  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    while (!request_queue.empty())
    {
      drained_requests.push_back(request_queue.front());
      request_queue.pop();
    }
  }

  if (!drained_requests.empty())
  {
    LOG_DEBUG("Worker %d: Drained %zu pending requests from queue", id, drained_requests.size());
  }

  return drained_requests;
}

std::string Worker::get_current_request()
{
  std::lock_guard<std::mutex> lock(current_request_mutex);
  return current_request_data;
}

// WorkerPool implementation
WorkerPool::WorkerPool(long long num_workers,
                       std::chrono::milliseconds request_timeout_param,
                       size_t max_replacement_attempts,
                       std::chrono::milliseconds base_replacement_backoff,
                       std::chrono::milliseconds max_replacement_backoff)
    : request_timeout(request_timeout_param <= std::chrono::milliseconds(0) ? std::chrono::seconds(60) : request_timeout_param),
      max_replace_attempts(max_replacement_attempts),
      base_replace_backoff(base_replacement_backoff),
      max_replace_backoff(max_replacement_backoff),
      max_live_detached(std::max<size_t>(2UL, static_cast<size_t>(num_workers > 0LL ? num_workers : 0LL)))
{
  workers.reserve(num_workers);

  // Create workers
  for (auto i = 0LL; i < num_workers; ++i)
  {
    auto worker = std::make_shared<Worker>(i);
    workers.push_back(std::move(worker));
  }
  ready_list.resize(num_workers, false);
  replacement_attempts.resize(num_workers, 0);
  next_replacement_allowed.resize(num_workers, std::chrono::steady_clock::time_point::min());

  // Initialize workers asynchronously
  for (auto i = 0LL; i < num_workers; ++i)
    spawn_initializer(static_cast<int>(i));

  if (num_workers > 0LL)
  {
    supervisor_running = true;
    supervisor_thread = std::thread(&WorkerPool::monitor_workers, this);
  }
}

WorkerPool::~WorkerPool()
{
  shutdown();
}

void WorkerPool::initialize_worker(int worker_id)
{
  if (worker_id < 0 || worker_id >= static_cast<int>(workers.size()))
  {
    LOG_ERROR("Invalid worker ID: %d", worker_id);
    return;
  }

  LOG_DEBUG("Initializing worker %d", worker_id);

  // Start the worker
  workers[worker_id]->start();

  // Mark worker as ready
  set_worker_ready(worker_id);
}

void WorkerPool::spawn_initializer(int worker_id)
{
  // Unlike a worker's request-processing thread, this never enters a z/OS system
  // service and can't get stuck, so it's always safe to join -- see shutdown().
  std::thread initializer_thread([this, worker_id]()
                                 {
    try
    {
      initialize_worker(worker_id);
    }
    catch (const std::system_error &e)
    {
      LOG_ERROR("Failed to initialize worker %d: %s", worker_id, e.what());
    } });

  std::lock_guard<std::mutex> lock(init_mutex);
  initializer_threads.push_back(std::move(initializer_thread));
}

void WorkerPool::distribute_request(const string &request)
{
  // Wrap the request in metadata with retry_count = 0
  RequestMetadata metadata(request, 0);
  distribute_request_internal(metadata);
}

void WorkerPool::distribute_request_internal(const RequestMetadata &request)
{
  if (is_shutting_down)
    return;

  // Simple round-robin distribution to ready workers
  Worker *worker = get_ready_worker();
  if (worker)
    worker->add_request(request);
}

Worker *WorkerPool::get_ready_worker()
{
  std::unique_lock<std::mutex> lock(ready_mutex);
  ready_condition.wait(lock, [this]
                       { return ready_count > 0 || is_shutting_down; });

  if (is_shutting_down)
    return nullptr;

  // Pop worker index from front for O(1) FIFO access (round-robin style)
  // Note: The queue may contain stale entries for workers that are no longer ready.
  // We validate each worker index as its popped from the queue.
  // This keeps the common path at O(1) while gracefully handling rare state transitions.
  while (!ready_queue.empty())
  {
    size_t worker_index = ready_queue.front();
    ready_queue.pop_front();

    // Validate worker is still ready and exists
    if (worker_index < workers.size() && workers[worker_index] && workers[worker_index]->is_ready())
    {
      // Re-add worker to back of queue to maintain round-robin distribution
      // Workers remain "ready" even while processing (Running state)
      ready_queue.push_back(worker_index);
      return workers[worker_index].get();
    }
    else
    {
      // Worker is no longer ready or doesn't exist, continue to next in queue
      LOG_DEBUG("Worker %zu popped from ready queue but is no longer ready", worker_index);
    }
  }

  return nullptr;
}

void WorkerPool::set_worker_ready(int worker_id)
{
  if (worker_id < 0)
  {
    LOG_ERROR("Attempted to mark invalid worker %d as ready (negative index)", worker_id);
    return;
  }

  bool notify_ready = false;

  {
    std::lock_guard<std::mutex> lock(ready_mutex);

    // Bounds checking for worker ID to make sure the pool hasn't shifted the index out-of-bounds
    if (worker_id >= static_cast<int>(workers.size()))
    {
      LOG_ERROR("Attempted to mark worker %d as ready but index is out of range", worker_id);
      return;
    }
    const auto worker_index = static_cast<size_t>(worker_id);

    // Make sure a worker still exists at that index in the pool before using it
    Worker *worker = workers[worker_index].get();
    if (!worker)
    {
      LOG_WARN("Attempted to mark worker %d as ready but worker slot is empty", worker_id);
      return;
    }

    if (worker_index >= ready_list.size())
      ready_list.resize(workers.size(), false);

    if (worker_index >= replacement_attempts.size())
    {
      replacement_attempts.resize(workers.size(), 0);
      next_replacement_allowed.resize(workers.size(), std::chrono::steady_clock::time_point::min());
    }

    if (!ready_list[worker_index] && worker->is_ready())
    {
      // Mark worker as ready if we haven't marked it in the list
      ready_list[worker_index] = true;
      ready_count.fetch_add(1);
      replacement_attempts[worker_index] = 0;
      next_replacement_allowed[worker_index] = std::chrono::steady_clock::time_point::min();

      ready_queue.push_back(worker_index);
      notify_ready = true;
    }
  }

  if (notify_ready)
  {
    // Notify `get_ready_worker` if its waiting for a worker to become available
    LOG_DEBUG("Worker %d marked as ready. Ready workers: %d", worker_id, ready_count.load());
    ready_condition.notify_one();
  }
}

int32_t WorkerPool::get_available_workers_count()
{
  return ready_count.load();
}

void WorkerPool::shutdown()
{
  LOG_DEBUG("Shutting down worker pool");
  is_shutting_down = true;
  ready_condition.notify_all();
  supervisor_running = false;

  if (supervisor_thread.joinable())
    supervisor_thread.join();

  // Initializer threads borrow `this`, so they must finish before the pool is
  // destroyed or they would touch freed members. Safe to join unconditionally:
  // unlike a worker's request-processing thread, initialize_worker() never
  // enters a z/OS system service, so it can't get stuck here.
  {
    std::lock_guard<std::mutex> lock(init_mutex);
    for (auto &initializer_thread : initializer_threads)
    {
      if (initializer_thread.joinable())
        initializer_thread.join();
    }
    initializer_threads.clear();
  }

  for (auto &worker : workers)
  {
    if (worker)
      worker->stop();
  }
  LOG_DEBUG("Worker pool shutdown complete");
}

void WorkerPool::monitor_worker_at(size_t i)
{
  Worker *worker = nullptr;

  {
    std::lock_guard<std::mutex> lock(ready_mutex);

    if (i < next_replacement_allowed.size() &&
        std::chrono::steady_clock::now() < next_replacement_allowed[i])
      return;

    if (i < workers.size())
      worker = workers[i].get();
  }

  if (worker == nullptr)
    return;

  const auto worker_state = worker->get_state();
  if (worker_state == WorkerState::Faulted)
  {
    replace_worker(i, "fault");
    return;
  }

  if (worker_state == WorkerState::Exited && !worker->is_stop_requested())
  {
    replace_worker(i, "unexpected exit");
    return;
  }

  if (worker_state == WorkerState::Running && request_timeout.count() > 0)
  {
    const auto last_heartbeat = worker->get_last_heartbeat();
    const auto now = std::chrono::steady_clock::now();
    if (last_heartbeat == std::chrono::steady_clock::time_point::min() || now <= last_heartbeat)
      return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat);
    if (elapsed > request_timeout)
    {
      LOG_WARN("Worker %zu exceeded request timeout (%lld ms > %lld ms); scheduling replacement", i, static_cast<long long>(elapsed.count()), static_cast<long long>(request_timeout.count()));
      replace_worker(i, "heartbeat timeout", true);
    }
  }
}

void WorkerPool::monitor_workers()
{
  // Scan over the workers periodically to check their heartbeat (worker state)
  while (supervisor_running && !is_shutting_down)
  {
    for (size_t i = 0; i < workers.size(); ++i)
    {
      if (!supervisor_running || is_shutting_down)
        break;

      monitor_worker_at(i);
    }

    // Sleep loop to avoid busy waiting on CPU
    for (int i = 0; i < 5 && supervisor_running && !is_shutting_down; i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool WorkerPool::mark_worker_not_ready(size_t worker_index)
{
  std::lock_guard<std::mutex> lock(ready_mutex);

  if (worker_index >= workers.size())
  {
    LOG_ERROR("Cannot mark worker at index %zu as not ready - out of range", worker_index);
    return false;
  }

  // Ensure vectors are sized appropriately
  if (worker_index >= ready_list.size())
    ready_list.resize(workers.size(), false);
  if (worker_index >= replacement_attempts.size())
  {
    replacement_attempts.resize(workers.size(), 0);
    next_replacement_allowed.resize(workers.size(), std::chrono::steady_clock::time_point::min());
  }

  // Decrement ready count if it was ready
  if (ready_list[worker_index])
  {
    ready_list[worker_index] = false;
    int32_t previous_ready = ready_count.fetch_sub(1);
    if (previous_ready <= 0)
    {
      // Critical: ready count and ready list are inconsistent
      // Reset count to 0 and abort replacement to prevent further corruption
      ready_count.store(0, std::memory_order_release);
      LOG_ERROR("Ready count inconsistency detected for worker %zu (was %d). Aborting replacement to prevent corruption.",
                worker_index, previous_ready);
      return false;
    }
  }

  return true;
}

struct ReplacementContext
{
  std::shared_ptr<Worker> old_worker;
  bool max_attempts_reached{false};
  size_t attempt_number{0UL};
  std::chrono::milliseconds applied_backoff{0};
};

ReplacementContext WorkerPool::prepare_worker_replacement(size_t worker_index)
{
  ReplacementContext ctx;
  std::lock_guard<std::mutex> lock(ready_mutex);

  if (worker_index >= workers.size())
  {
    LOG_ERROR("Cannot prepare worker replacement at index %zu - out of range", worker_index);
    return ctx;
  }

  const size_t attempts = replacement_attempts[worker_index];
  const auto now = std::chrono::steady_clock::now();

  if (attempts >= max_replace_attempts)
  {
    ctx.max_attempts_reached = true;
  }
  else
  {
    // Not max attempts, so schedule the next backoff time
    ctx.attempt_number = attempts + 1;
    replacement_attempts[worker_index] = ctx.attempt_number;

    auto computed_backoff = base_replace_backoff * (1ULL << (ctx.attempt_number - 1));
    if (computed_backoff > max_replace_backoff)
      computed_backoff = max_replace_backoff;

    ctx.applied_backoff = computed_backoff;
    next_replacement_allowed[worker_index] = now + computed_backoff;
  }

  // Move worker out to be processed outside the lock
  ctx.old_worker = std::move(workers[worker_index]);
  return ctx;
}

std::vector<RequestMetadata> WorkerPool::recover_requests_from_worker(
    std::shared_ptr<Worker> &old_worker,
    size_t worker_index,
    const char *reason,
    bool force_detach)
{
  std::vector<RequestMetadata> recovered_requests;

  if (!old_worker)
    return recovered_requests;

  // Get pending requests from queue
  auto pending = old_worker->drain_pending_requests();
  recovered_requests.insert(recovered_requests.end(), pending.begin(), pending.end());

  // Recover in-flight request only if it wasn't a timeout/hang
  std::string current_req = old_worker->get_current_request();
  if (!current_req.empty())
  {
    if (!force_detach)
    {
      LOG_DEBUG("Worker %zu: Recovering in-flight request due to %s", worker_index, reason);
      recovered_requests.emplace_back(current_req, 0);
    }
    else
    {
      LOG_DEBUG("Worker %zu: Sending timeout error for in-flight request", worker_index);
      RpcServer &server = RpcServer::get_instance();
      server.send_timeout_error(current_req, request_timeout.count());
    }
  }

  // Stop/detach the old worker. force_detach() already detaches the thread, which
  // leaves stop() with nothing to join, so only one of the two applies.
  if (force_detach)
    old_worker->force_detach();
  else
    old_worker->stop();

  return recovered_requests;
}

void WorkerPool::spawn_replacement_worker(size_t worker_index)
{
  auto new_worker = std::make_shared<Worker>(static_cast<int>(worker_index));

  {
    std::lock_guard<std::mutex> lock(ready_mutex);

    // Re-check sizes in case of resize
    if (worker_index >= workers.size())
      workers.resize(worker_index + 1);
    if (worker_index >= ready_list.size())
      ready_list.resize(worker_index + 1, false);
    if (worker_index >= replacement_attempts.size())
    {
      replacement_attempts.resize(worker_index + 1, 0);
      next_replacement_allowed.resize(worker_index + 1, std::chrono::steady_clock::time_point::min());
    }

    workers[worker_index] = std::move(new_worker);
    ready_list[worker_index] = false;
  }

  spawn_initializer(static_cast<int>(worker_index));
  LOG_DEBUG("Replacement worker %zu spawned, awaiting readiness", worker_index);
}

void WorkerPool::enforce_detached_worker_limit()
{
  const size_t live = Worker::live_detached_count();
  if (live <= max_live_detached)
    return;

  LOG_FATAL("%zu worker thread(s) are wedged and could not be reclaimed (limit %zu). "
            "Exiting so the system can release them; the client will reconnect to a new server.",
            live, max_live_detached);

  // Flush the log before leaving. _exit() rather than exit(): we are on the
  // supervisor thread, and the atexit handler would call shutdown(), which joins
  // the supervisor thread and would therefore deadlock on itself.
  ZLogger::get_instance().shutdown();
  _exit(kDetachedWorkerLimitExitCode);
}

void WorkerPool::replace_worker(size_t worker_index, const char *reason, bool force_detach)
{
  if (is_shutting_down.load())
    return;

  // Mark worker as not ready and decrement ready count
  if (!mark_worker_not_ready(worker_index))
    return;

  // Check retry limits and prepare replacement context
  ReplacementContext ctx = prepare_worker_replacement(worker_index);

  // Recover requests and stop the old worker
  std::vector<RequestMetadata> recovered_requests =
      recover_requests_from_worker(ctx.old_worker, worker_index, reason, force_detach);

  if (force_detach)
  {
    // Does not return if too many threads are wedged
    enforce_detached_worker_limit();
  }

  // Don't create new worker if we exceeded max attempts
  if (ctx.max_attempts_reached)
  {
    LOG_ERROR("Worker %zu hit maximum replacement attempts (%zu) after %s; leaving worker offline",
              worker_index, max_replace_attempts, reason);
    if (!recovered_requests.empty())
      redistribute_requests(recovered_requests, worker_index, reason);
    return;
  }

  if (ctx.attempt_number > 0)
  {
    LOG_WARN("Replacing worker %zu due to %s (attempt %zu, next backoff %lld ms)",
             worker_index, reason, ctx.attempt_number,
             static_cast<long long>(ctx.applied_backoff.count()));
  }

  if (is_shutting_down.load())
    return;

  // Create and spawn the new worker
  spawn_replacement_worker(worker_index);

  // Redistribute recovered requests from old worker
  if (!recovered_requests.empty())
    redistribute_requests(recovered_requests, worker_index, reason);
}

void WorkerPool::redistribute_requests(std::vector<RequestMetadata> &requests, size_t worker_index, const char *reason)
{
  if (is_shutting_down.load())
    return;

  size_t redistributed_count = 0;
  size_t failed_count = 0;

  for (auto &req : requests)
  {
    // Check if request has exceeded max retry limit (poison pill protection)
    if (req.retry_count >= kMaxRequestRetries)
    {
      LOG_ERROR("Request from worker %zu discarded after %zu retry attempts (poison pill protection). Reason: %s",
                worker_index, req.retry_count, reason);
      failed_count++;
      continue;
    }

    // Increment retry count and re-distribute
    req.retry_count++;
    LOG_DEBUG("Re-routing request from worker %zu (attempt %zu/%zu). Reason: %s",
              worker_index, req.retry_count, kMaxRequestRetries, reason);

    distribute_request_internal(req);
    redistributed_count++;
  }

  if (redistributed_count > 0)
  {
    LOG_INFO("Redistributed %zu requests from failed worker %zu. Failed: %zu (poison pills)",
             redistributed_count, worker_index, failed_count);
  }
}

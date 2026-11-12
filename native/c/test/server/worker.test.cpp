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

#include "worker.test.hpp"
#include "../ztest.hpp"
#include "../../server/worker.hpp"
#include "../../zlogger.hpp"

#include <string>
#include <stdexcept>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace ztst;

/**
 * @brief Stub for rpc_server.hpp
 *
 * Provides a mock RpcServer singleton that worker.cpp depends on.
 * This mock is controllable to simulate successful work, faults (exceptions),
 * and hangs (timeouts).
 */
#ifndef RPC_SERVER_HPP
#define RPC_SERVER_HPP

class RpcServer
{
public:
  // If true, requests with data "hang" will loop until this is set to false
  std::atomic<bool> hang_request{false};

  // Counts how many requests have been fully processed
  std::atomic<int> processed_count{0};

  // Counts how many timeout errors have been sent to clients
  std::atomic<int> timeout_error_count{0};

  // Stores the last request data received
  std::string last_processed_request;
  std::mutex mtx; // Protects last_processed_request

  /**
   * @brief Get the singleton instance.
   */
  static RpcServer &get_instance()
  {
    static RpcServer instance;
    return instance;
  }

  /**
   * @brief The mock request processing logic.
   *
   * @param data The request payload.
   * @param heartbeat_callback Unused by this mock; accepted for signature compatibility.
   */
  void process_request(const std::string &data, std::function<void()> heartbeat_callback = nullptr)
  {
    (void)heartbeat_callback;
    {
      std::lock_guard<std::mutex> lock(mtx);
      last_processed_request = data;
    }

    // Simulate a hang (for timeout tests)
    if (data == "hang")
    {
      hang_request.store(true);
      TestLog("RpcServer: Simulating hang. Request 'hang' received.");
      while (hang_request.load())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      TestLog("RpcServer: Hang released.");
      processed_count++; // Count it once it's "finished"
      return;
    }

    // Simulate a fault (for exception tests)
    if (data == "fault")
    {
      TestLog("RpcServer: Simulating fault. Request 'fault' received.");
      processed_count++; // Count it as "processed" before throwing
      throw std::runtime_error("Simulated worker fault");
    }

    // Default behavior (simulating normal work)
    // Sleep for a short duration to make state transitions observable
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    processed_count++;
  }

  /**
   * @brief Mock implementation of send_timeout_error.
   * Tracks timeout errors sent to clients for test validation.
   *
   * @param request_data The raw JSON-RPC request string that timed out
   * @param timeout_ms The timeout value that was exceeded (in milliseconds)
   */
  void send_timeout_error(const std::string &request_data, int64_t timeout_ms)
  {
    timeout_error_count++;
    TestLog("RpcServer: Timeout error sent to client for request");
    (void)request_data; // Suppress unused parameter warning
  }

  /**
   * @brief Mock implementation of set_heartbeat_callback.
   * No-op; heartbeat behavior for streaming operations isn't exercised by this test.
   */
  static void set_heartbeat_callback(std::function<void()> callback)
  {
    (void)callback;
  }

  /**
   * @brief Resets the mock server's state.
   * Called before tests.
   */
  void reset()
  {
    hang_request.store(false);
    processed_count.store(0);
    timeout_error_count.store(0);
    std::lock_guard<std::mutex> lock(mtx);
    last_processed_request.clear();
  }

private:
  // Private constructor/destructor for singleton
  RpcServer() = default;
  ~RpcServer() = default;
  RpcServer(const RpcServer &) = delete;
  RpcServer &operator=(const RpcServer &) = delete;
};
#endif // RPC_SERVER_HPP

using namespace std::chrono_literals;

/**
 * @brief Helper function to wait for an asynchronous condition with a timeout.
 *
 * @param condition A function returning true when the condition is met.
 * @param timeout The maximum time to wait.
 * @return true if the condition was met, false if it timed out.
 */
bool wait_for(std::function<bool()> condition, std::chrono::milliseconds timeout)
{
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout)
  {
    if (condition())
    {
      return true;
    }
    std::this_thread::sleep_for(10ms); // Poll interval
  }
  return false;
}

/**
 * @brief Main function containing all test definitions for Worker and WorkerPool.
 */
void server_worker_tests()
{
  // Global test state shared across tests in this file
  std::shared_ptr<WorkerPool> pool;
  RpcServer &server = RpcServer::get_instance();

  // Reset the mock RpcServer before each test
  beforeEach([&]()
  {
    TestLog("Resetting RpcServer mock state...");
    server.reset();
  });

  // Ensure the pool is shut down after each test to clean up threads
  afterEach([&]()
  {
    TestLog("Shutting down worker pool...");
    if (pool)
    {
      pool->shutdown();
      pool = nullptr;
    }
  });

  describe("Worker", [&]()
  {
    std::shared_ptr<Worker> worker;

    // Clean up individual worker after each 'it' block
    afterEach([&]()
    {
      if (worker)
      {
        worker->stop();
        worker = nullptr;
      }
    });

    it("should start and transition to Idle state", [&]()
    {
      worker = std::make_shared<Worker>(0);
      // Initial state is Starting (set in constructor)
      Expect(worker->get_state() == WorkerState::Starting).ToBe(true);

      worker->start();

      // After start(), the loop runs and waits, setting state to Idle
      bool became_idle = wait_for([&]()
      {
        return worker->get_state() == WorkerState::Idle;
      }, 500ms);
      Expect(became_idle).ToBe(true);
    });

    it("should process a request and return to Idle", [&]()
    {
      worker = std::make_shared<Worker>(0);
      worker->start();
      Expect(worker->get_state() == WorkerState::Idle).ToBe(true);

      server.reset();
      worker->add_request(RequestMetadata("test_request_1"));

      // Wait for the worker to pick up the request and go to Running
      bool became_running = wait_for([&]()
      {
        return worker->get_state() == WorkerState::Running;
      }, 500ms);
      Expect(became_running).ToBe(true);

      // Wait for it to finish processing and go back to Idle
      bool became_idle = wait_for([&]()
      {
        return worker->get_state() == WorkerState::Idle;
      }, 500ms);
      Expect(became_idle).ToBe(true);

      // Verify the mock server actually processed it
      Expect(server.processed_count.load()).ToBe(1);
    });

    it("should transition to Faulted state on exception", [&]()
    {
      worker = std::make_shared<Worker>(0);
      worker->start();
      Expect(worker->get_state() == WorkerState::Idle).ToBe(true);

      server.reset();
      // "fault" is a special command for the mock RpcServer to throw
      worker->add_request(RequestMetadata("fault"));

      // Wait for the worker's catch block to set the state
      bool faulted = wait_for([&]()
      {
        return worker->get_state() == WorkerState::Faulted;
      }, 500ms);

      Expect(faulted).ToBe(true);
      Expect(server.processed_count.load()).ToBe(1);
    });

    it("should stop cleanly and transition to Exited state", [&]()
    {
      worker = std::make_shared<Worker>(0);
      worker->start();
      Expect(worker->get_state() == WorkerState::Idle).ToBe(true);

      worker->stop();

      // Note: stop() joins the thread, so this is synchronous
      Expect(worker->get_state() == WorkerState::Exited).ToBe(true);
    });
  });

  describe("WorkerPool integration", [&]()
  {
    it("should initialize and all workers become ready", [&]()
    {
      long long num_workers = 4LL;
      pool = std::make_shared<WorkerPool>(num_workers, 1000ms);

      // Wait for all workers to start and mark themselves as ready
      bool all_ready = wait_for([&]()
      {
        return pool->get_available_workers_count() == num_workers;
      }, 2000ms); // Give them time to start up

      Expect(all_ready).ToBe(true);
      Expect(pool->get_available_workers_count()).ToBe(num_workers);
    });

    it("should distribute multiple requests to workers", [&]()
    {
      long long num_workers = 2LL;
      pool = std::make_shared<WorkerPool>(num_workers, 1000ms);

      // Wait for workers to be ready
      bool all_ready = wait_for([&]()
      {
        return pool->get_available_workers_count() == num_workers;
      }, 1000ms);
      Expect(all_ready).ToBe(true);

      server.reset();
      pool->distribute_request("req1");
      pool->distribute_request("req2");
      pool->distribute_request("req3");

      // Wait for all three requests to be processed
      bool all_processed = wait_for([&]()
      {
        return server.processed_count.load() == 3;
      }, 1000ms);

      Expect(all_processed).ToBe(true);
      Expect(server.processed_count.load()).ToBe(3);
    });

    it("should replace a faulted worker and redistribute its requests", [&]()
    {
      long long num_workers = 1LL;
      // Use a long timeout so it doesn't interfere with the fault test
      pool = std::make_shared<WorkerPool>(num_workers, 5000ms);

      bool ready = wait_for([&]()
      {
        return pool->get_available_workers_count() == num_workers;
      }, 1000ms);
      Expect(ready).ToBe(true);

      server.reset();
      // Add some requests that will be pending in the queue
      pool->distribute_request("pending1");
      pool->distribute_request("pending2");
      // Add the request that will cause the fault
      pool->distribute_request("fault");

      // Expected sequence:
      // 1. "fault" is processed (count=1), worker 0 faults.
      // 2. Monitor thread detects fault (~500ms loop).
      // 3. `replace_worker` is called.
      // 4. "pending1" and "pending2" are drained from the queue.
      // 5. "fault" (the in-flight request) is *recovered* (since force_detach=false).
      // 6. New worker 0 is created and started.
      // 7. "pending1", "pending2", and "fault" (retry 1) are redistributed.
      // 8. "pending1" is processed (count=2).
      // 9. "pending2" is processed (count=3).
      // 10. "fault" (retry 1) is processed (count=4), new worker 0 faults.
      // 11. Monitor detects fault, replaces worker.
      // 12. "fault" (retry 2) is recovered and redistributed.
      // 13. "fault" (retry 2) is processed (count=5), new worker 0 faults.
      // 14. Monitor detects fault, replaces worker.
      // 15. "fault" (retry 3) is recovered, but `kMaxRequestRetries` (2) is exceeded.
      // 16. "fault" request is DISCARDED (poison pill).

      TestLog("Waiting for fault/recovery/redistribution/poison pill cycle...");
      // We expect 5 total requests to be processed
      bool finished_processing = wait_for([&]()
      {
        return server.processed_count.load() == 5; // fault, p1, p2, fault(r1), fault(r2)
      }, 5000ms);                                  // Needs time for monitor loops + replacement backoffs

      Expect(finished_processing).ToBe(true);
      Expect(server.processed_count.load()).ToBe(5);

      // Now, send a clean request. It should be processed by a new, healthy worker.
      TestLog("Sending clean request after poison pill...");
      pool->distribute_request("clean");

      bool clean_processed = wait_for([&]()
      {
        return server.processed_count.load() == 6;
      }, 2000ms); // Replacement backoff might add delay

      Expect(clean_processed).ToBe(true);
      Expect(server.processed_count.load()).ToBe(6);
    });

    it("should replace a timed-out worker, send timeout error to client, and NOT recover in-flight request", [&]()
    {
      int num_workers = 1;
      // Use a very short timeout for the test
      auto short_timeout = 250ms;
      pool = std::make_shared<WorkerPool>(num_workers, short_timeout);

      bool ready = wait_for([&]()
      {
        return pool->get_available_workers_count() == num_workers;
      }, 1000ms);
      Expect(ready).ToBe(true);

      server.reset();
      // Add a pending request
      pool->distribute_request("pending1");
      // Add the request that will hang
      pool->distribute_request("hang");

      // Expected sequence:
      // 1. "pending1" is processed normally (count=1).
      // 2. "hang" is processed, RpcServer loop starts, worker state is "Running".
      // 3. Monitor thread detects stale heartbeat (~250ms + ~500ms loop).
      // 4. `replace_worker` is called with `force_detach=true`.
      // 5. Worker pool calls `send_timeout_error` for the hanging request (timeout_error_count=1).
      // 6. "hang" (in-flight) is *NOT* recovered due to `force_detach`.
      // 7. New worker 0 is created and started.
      // 8. No additional requests are redistributed (pending queue was empty after step 1).

      TestLog("Waiting for timeout/replacement cycle and timeout error...");

      // First, verify that pending1 was processed and timeout error was sent
      bool timeout_sent = wait_for([&]()
      {
        return server.processed_count.load() >= 1 && server.timeout_error_count.load() >= 1;
      }, 3000ms); // Needs time for timeout + monitor + backoff

      Expect(timeout_sent).ToBe(true);
      Expect(server.processed_count.load()).ToBe(1);     // Only "pending1" processed normally
      Expect(server.timeout_error_count.load()).ToBe(1); // Timeout error sent for "hang" request

      TestLog("Timeout error successfully sent to client - preventing client hang!");

      // Now, manually release the original hanging request
      TestLog("Releasing original hang request...");
      server.hang_request.store(false);

      // Wait a bit. The processed count should increase to 2 as the
      // *original detached* thread finally finishes its `process_request` call.
      // This confirms the original thread was left to run.
      bool hang_finished = wait_for([&]()
      {
        return server.processed_count.load() == 2;
      }, 500ms);

      Expect(hang_finished).ToBe(true);
      Expect(server.processed_count.load()).ToBe(2);

      // The pool should now have a healthy, ready worker.
      TestLog("Sending clean request after timeout recovery...");
      pool->distribute_request("clean");
      bool clean_processed = wait_for([&]()
      {
        return server.processed_count.load() == 3;
      }, 1000ms);

      Expect(clean_processed).ToBe(true);
      Expect(server.processed_count.load()).ToBe(3);
      Expect(pool->get_available_workers_count()).ToBe(1);

      // Final validation: exactly one timeout error was sent
      Expect(server.timeout_error_count.load()).ToBe(1);
    });
  });
}
// Include the implementation so the stubs above satisfy the real compilation unit.
#include "../server/worker.cpp"

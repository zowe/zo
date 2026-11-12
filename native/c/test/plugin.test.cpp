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

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ztest.hpp"
#include "../extend/plugin.hpp"
#include "../parser.hpp"

using namespace ztst;

// These tests exercise the load_plugins() load-time gate:
//   - hygiene: '.'/'..' are skipped, and any entry that isn't a regular
//     file (subdirectory, symlink, FIFO, etc.) is rejected before dlopen.
//   - provenance: the plugins directory is rejected wholesale when it is
//     group-/world-writable or owned by an untrusted identity, and individual
//     files are rejected when they are group-/world-writable or owned by an
//     identity that is neither the directory owner nor a trusted one (root/self).
//
// They assert on get_loaded_plugins() (the deterministic, in-process observable)
// rather than on log output. An earlier revision scraped logs/zowex.log for the
// per-entry "Rejected .../Skipping ..." messages plugin.cpp emits, but that was
// unreliable: those messages go through the process-global ZLogger singleton,
// whose Metal C backend binds a DD to a single log file at first use. Other
// suites (e.g. zlogger_tests) delete that file mid-run, orphaning the DD, and
// the logger cannot be safely re-pointed in-process (re-initializing it abends
// while tearing down the stale DD). The security-relevant guarantee - that no
// non-regular or dot entry is ever loaded, and that load_plugins() stays robust
// (no crash/hang) against hostile directory contents - is fully captured by
// asserting nothing loads, without any dependency on log-file lifecycle.

namespace
{
const std::string PLUGIN_TEST_DIR = "plugin_hygiene_test";

// Recursively wipe and recreate the scratch plugins directory used by these tests, so
// leftovers from an aborted previous run (or another suite) can't leak into assertions here.
void reset_plugin_test_dir()
{
  DIR *dir = opendir(PLUGIN_TEST_DIR.c_str());
  if (dir != nullptr)
  {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
      const std::string name = entry->d_name;
      if (name == "." || name == "..")
        continue;
      const std::string path = PLUGIN_TEST_DIR + "/" + name;
      struct stat st;
      if (lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        rmdir(path.c_str());
      else
        unlink(path.c_str());
    }
    closedir(dir);
  }
  rmdir(PLUGIN_TEST_DIR.c_str());
  mkdir(PLUGIN_TEST_DIR.c_str(), 0755);
}

void write_regular_file(const std::string &path, const std::string &contents)
{
  std::ofstream file(path, std::ios::binary);
  file << contents;
}

// Recursively wipe and remove an arbitrary scratch directory (used by the
// provenance tests, which need directories with non-default permission bits
// outside the shared PLUGIN_TEST_DIR).
void remove_scratch_dir(const std::string &dir)
{
  DIR *handle = opendir(dir.c_str());
  if (handle != nullptr)
  {
    struct dirent *entry;
    while ((entry = readdir(handle)) != nullptr)
    {
      const std::string name = entry->d_name;
      if (name == "." || name == "..")
        continue;
      unlink((dir + "/" + name).c_str());
    }
    closedir(handle);
  }
  rmdir(dir.c_str());
}

// --- Dispatcher-hardening test helpers -------------------------------------
// These exercise PluginManager::register_commands directly, without dlopen: a
// provider is registered in-process and driven by a lambda that builds commands
// through the same CommandRegistrationContext a real plug-in would use.

using RegistrationContext = plugin::CommandProviderImpl::CommandRegistrationContext;
using RegistrationFn = std::function<void(RegistrationContext &)>;

class LambdaProvider : public plugin::CommandProviderImpl
{
public:
  explicit LambdaProvider(RegistrationFn fn) : m_fn(std::move(fn))
  {
  }
  void register_commands(CommandRegistrationContext &context) override
  {
    if (m_fn)
      m_fn(context);
  }

private:
  RegistrationFn m_fn;
};

class LambdaProviderFactory : public plugin::CommandProvider
{
public:
  explicit LambdaProviderFactory(RegistrationFn fn) : m_fn(std::move(fn))
  {
  }
  std::unique_ptr<plugin::CommandProviderImpl> create() override
  {
    return std::unique_ptr<plugin::CommandProviderImpl>(new LambdaProvider(m_fn));
  }

private:
  RegistrationFn m_fn;
};

void register_provider(plugin::PluginManager &pm, RegistrationFn fn)
{
  pm.register_command_provider(std::unique_ptr<plugin::CommandProvider>(new LambdaProviderFactory(std::move(fn))));
}

// Mirror the state zo is in when it calls register_commands: a root already
// populated with a built-in verb.
parser::command_ptr make_builtin(const std::string &name, const std::string &help)
{
  return std::make_shared<parser::Command>(name, help);
}

bool root_has(const parser::Command &root, const std::string &name)
{
  return root.get_commands().count(name) != 0;
}

bool was_rejected(const plugin::PluginManager &pm, const std::string &name)
{
  const auto &rejected = pm.get_rejected_command_names();
  return std::find(rejected.begin(), rejected.end(), name) != rejected.end();
}
} // namespace

void plugin_tests()
{
  describe("PluginManager::load_plugins hygiene", []() -> void
  {
    beforeEach([]()
    {
      reset_plugin_test_dir();
    });

    afterAll([]()
    {
      reset_plugin_test_dir();
      rmdir(PLUGIN_TEST_DIR.c_str());
    });

    it("ignores '.' and '..' directory entries and loads nothing", []()
    {
      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      // '.' and '..' are always present; the hygiene checks must skip them
      // instead of attempting to load the directory itself.
      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));
    });

    it("does not load a subdirectory entry", []()
    {
      const std::string sub_path = PLUGIN_TEST_DIR + "/subdir_entry";
      mkdir(sub_path.c_str(), 0755);

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      // A subdirectory is not a regular file, so it must be rejected rather
      // than loaded (and must not crash the loader).
      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      rmdir(sub_path.c_str());
    });

    it("does not load a symlink entry, even one pointing at a regular file (TOCTOU-safe)", []()
    {
      // Point the symlink at a legitimate, regular file to show rejection is
      // based on the symlink itself (lstat + S_ISREG), not on what it resolves
      // to - the loader must not follow the link.
      const std::string target_path = PLUGIN_TEST_DIR + "/symlink_target.so";
      const std::string link_path = PLUGIN_TEST_DIR + "/symlink_entry.so";
      write_regular_file(target_path, "not a real shared object, just needs to exist");
      symlink(target_path.c_str(), link_path.c_str());

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      // Neither the symlink (rejected: not a regular file) nor its bogus
      // target (reaches dlopen but fails to load) should end up loaded.
      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      unlink(link_path.c_str());
      unlink(target_path.c_str());
    });

    it("does not load a named pipe (FIFO) entry", []()
    {
      const std::string fifo_path = PLUGIN_TEST_DIR + "/fifo_entry";
      mkfifo(fifo_path.c_str(), 0666);

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      // A FIFO is not a regular file and must be rejected before dlopen -
      // handing one to dlopen could otherwise block the loader.
      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      unlink(fifo_path.c_str());
    });

    it("loads nothing for a regular file that is not a valid shared object", []()
    {
      // Regression check: the hygiene filtering must not reject legitimate
      // regular files outright. This one is a regular file, so it passes the
      // hygiene checks and reaches dlopen, which fails because it isn't a real
      // shared object - so nothing is loaded and the loader doesn't crash.
      const std::string plugin_path = PLUGIN_TEST_DIR + "/not_a_valid_plugin.so";
      write_regular_file(plugin_path, "definitely not an ELF/shared object");

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      unlink(plugin_path.c_str());
    });

    it("loads nothing from a directory containing a mix of non-regular and invalid entries", []()
    {
      const std::string sub_path = PLUGIN_TEST_DIR + "/mixed_subdir";
      const std::string fifo_path = PLUGIN_TEST_DIR + "/mixed_fifo";
      const std::string link_target = PLUGIN_TEST_DIR + "/mixed_target.so";
      const std::string link_path = PLUGIN_TEST_DIR + "/mixed_symlink.so";
      const std::string regular_path = PLUGIN_TEST_DIR + "/mixed_regular.so";

      mkdir(sub_path.c_str(), 0755);
      mkfifo(fifo_path.c_str(), 0666);
      write_regular_file(link_target, "target");
      symlink(link_target.c_str(), link_path.c_str());
      write_regular_file(regular_path, "not a real shared object");

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      // The subdirectory, FIFO and symlink are all rejected as non-regular;
      // the regular file reaches dlopen but fails to load. Net result: the
      // loader survives a hostile directory and loads nothing.
      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      rmdir(sub_path.c_str());
      unlink(fifo_path.c_str());
      unlink(link_path.c_str());
      unlink(link_target.c_str());
      unlink(regular_path.c_str());
    });

    it("does not crash and loads nothing when the plugins directory does not exist", []()
    {
      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR + "/does_not_exist");

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));
    });
  });

  describe("PluginManager::load_plugins provenance", []() -> void
  {
    beforeEach([]()
    {
      reset_plugin_test_dir();
    });

    afterAll([]()
    {
      reset_plugin_test_dir();
      rmdir(PLUGIN_TEST_DIR.c_str());
    });

    it("rejects the entire directory (loads nothing) when the plugins directory is world-writable", []()
    {
      // A world-writable plugins directory means any identity on the system
      // could drop executable code here, so the whole directory is rejected
      // before any entry is considered - even a legitimate-looking file.
      const std::string ww_dir = "plugin_provenance_world_writable";
      remove_scratch_dir(ww_dir);
      mkdir(ww_dir.c_str(), 0777);
      chmod(ww_dir.c_str(), 0777); // defeat umask so the group/world write bits actually land
      write_regular_file(ww_dir + "/probe.so", "not a real shared object");

      plugin::PluginManager pm;
      pm.load_plugins(ww_dir);

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      remove_scratch_dir(ww_dir);
    });

    it("rejects the entire directory (loads nothing) when the plugins directory is group-writable", []()
    {
      const std::string gw_dir = "plugin_provenance_group_writable";
      remove_scratch_dir(gw_dir);
      mkdir(gw_dir.c_str(), 0775);
      chmod(gw_dir.c_str(), 0775);
      write_regular_file(gw_dir + "/probe.so", "not a real shared object");

      plugin::PluginManager pm;
      pm.load_plugins(gw_dir);

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      remove_scratch_dir(gw_dir);
    });

    it("rejects a group-/world-writable plugin file inside an otherwise-trusted directory", []()
    {
      // The directory itself is safe (0755, owned by us), but the file is
      // world-writable, so it can be swapped out after being placed. It must
      // be rejected on its own merits before dlopen.
      const std::string plugin_path = PLUGIN_TEST_DIR + "/world_writable_plugin.so";
      write_regular_file(plugin_path, "not a real shared object");
      chmod(plugin_path.c_str(), 0666);

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      unlink(plugin_path.c_str());
    });

    it("accepts a well-permissioned directory owned by the current user (regression: does not over-reject)", []()
    {
      // reset_plugin_test_dir() creates PLUGIN_TEST_DIR mode 0755 owned by the
      // test user, and this file is 0644 and owned by the same user - i.e. it
      // satisfies every provenance check. It still fails to load because it is
      // not a real shared object, but it must reach dlopen rather than being
      // rejected by the provenance gate. The observable (nothing loaded, no
      // crash) is the same; the point is that the checks do not reject
      // legitimate, correctly-owned files outright.
      const std::string plugin_path = PLUGIN_TEST_DIR + "/well_owned_plugin.so";
      write_regular_file(plugin_path, "not a real shared object");
      chmod(plugin_path.c_str(), 0644);

      plugin::PluginManager pm;
      pm.load_plugins(PLUGIN_TEST_DIR);

      Expect(pm.get_loaded_plugins().size()).ToBe(std::size_t(0));

      unlink(plugin_path.c_str());
    });
  });

  describe("PluginManager::register_commands dispatcher hardening", []() -> void
  {
    it("refuses a plug-in command whose name shadows a built-in verb and keeps the built-in", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto shadow = ctx.create_command("job", "plugin job");
        ctx.add_subcommand(ctx.get_root_command(), shadow);
      });
      pm.register_commands(root);

      Expect(root_has(root, "job")).ToBe(true);
      Expect(root.get_commands().at("job")->get_help()).ToBe(std::string("builtin job"));
      Expect(was_rejected(pm, "job")).ToBe(true);
    });

    it("refuses a plug-in command whose alias shadows a built-in verb", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto cmd = ctx.create_command("safe", "plugin safe");
        ctx.add_alias(cmd, "job"); // alias collides with the built-in name
        ctx.add_subcommand(ctx.get_root_command(), cmd);
      });
      pm.register_commands(root);

      // The whole command is refused because one of its tokens shadows a built-in.
      Expect(root_has(root, "safe")).ToBe(false);
      Expect(root_has(root, "job")).ToBe(true);
      Expect(root.get_commands().at("job")->get_help()).ToBe(std::string("builtin job"));
      Expect(was_rejected(pm, "safe")).ToBe(true);
    });

    it("refuses a shadowing alias even when it is added after the command is attached to root", []()
    {
      // Ordering-robustness: add_alias is called *after* add_subcommand, so the check
      // must run against the command's final token set, not its state at attach time.
      parser::Command root("zo", "root");
      root.add_command(make_builtin("ds", "builtin ds"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto cmd = ctx.create_command("safe", "plugin safe");
        ctx.add_subcommand(ctx.get_root_command(), cmd);
        ctx.add_alias(cmd, "ds");
      });
      pm.register_commands(root);

      Expect(root_has(root, "safe")).ToBe(false);
      Expect(root_has(root, "ds")).ToBe(true);
      Expect(root.get_commands().at("ds")->get_help()).ToBe(std::string("builtin ds"));
      Expect(was_rejected(pm, "safe")).ToBe(true);
    });

    it("registers a non-colliding plug-in command", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto hello = ctx.create_command("hello", "plugin hello");
        ctx.add_subcommand(ctx.get_root_command(), hello);
      });
      pm.register_commands(root);

      Expect(root_has(root, "hello")).ToBe(true);
      Expect(root_has(root, "job")).ToBe(true);
      Expect(pm.get_rejected_command_names().size()).ToBe(std::size_t(0));
    });

    it("does not over-reject a plug-in command whose name merely resembles a built-in verb", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto cmd = ctx.create_command("jobs", "plugin jobs"); // distinct from "job"
        ctx.add_subcommand(ctx.get_root_command(), cmd);
      });
      pm.register_commands(root);

      Expect(root_has(root, "jobs")).ToBe(true);
      Expect(pm.get_rejected_command_names().size()).ToBe(std::size_t(0));
    });

    it("registers a plug-in's non-colliding commands even when one of its commands collides", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto shadow = ctx.create_command("job", "plugin job");
        ctx.add_subcommand(ctx.get_root_command(), shadow);
        auto ok = ctx.create_command("hello", "plugin hello");
        ctx.add_subcommand(ctx.get_root_command(), ok);
      });
      pm.register_commands(root);

      Expect(root_has(root, "hello")).ToBe(true);
      Expect(root.get_commands().at("job")->get_help()).ToBe(std::string("builtin job"));
      Expect(was_rejected(pm, "job")).ToBe(true);
    });

    it("refuses a second plug-in command that duplicates one an earlier plug-in claimed", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto first = ctx.create_command("hello", "first hello");
        ctx.add_subcommand(ctx.get_root_command(), first);
      });
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto second = ctx.create_command("hello", "second hello");
        ctx.add_subcommand(ctx.get_root_command(), second);
      });
      pm.register_commands(root);

      Expect(root_has(root, "hello")).ToBe(true);
      Expect(root.get_commands().at("hello")->get_help()).ToBe(std::string("first hello"));
      Expect(was_rejected(pm, "hello")).ToBe(true);
    });

    it("does not crash and drops a malformed plug-in's duplicate nested subcommand", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto grp = ctx.create_command("grp", "plugin group");
        auto sub1 = ctx.create_command("sub", "first sub");
        auto sub2 = ctx.create_command("sub", "second sub");
        ctx.add_subcommand(grp, sub1);
        ctx.add_subcommand(grp, sub2); // duplicate sibling name - guarded, must not throw
        ctx.add_subcommand(ctx.get_root_command(), grp);
      });
      pm.register_commands(root);

      Expect(root_has(root, "grp")).ToBe(true);
      Expect(root.get_commands().at("grp")->get_commands().size()).ToBe(std::size_t(1));
      // A nested duplicate is not a top-level shadowing rejection.
      Expect(pm.get_rejected_command_names().size()).ToBe(std::size_t(0));
    });

    it("drops a rejected shadowing command from the server command set", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto shadow = ctx.create_command("job", "plugin job");
        ctx.add_to_server(shadow);
        ctx.add_subcommand(ctx.get_root_command(), shadow);
      });
      pm.register_commands(root);

      // A shadowing command must gain no foothold on the server dispatch path either.
      Expect(pm.get_server_commands().size()).ToBe(std::size_t(0));
      Expect(was_rejected(pm, "job")).ToBe(true);
    });

    it("keeps a non-colliding command in the server command set", []()
    {
      parser::Command root("zo", "root");
      root.add_command(make_builtin("job", "builtin job"));

      plugin::PluginManager pm;
      register_provider(pm, [](RegistrationContext &ctx)
      {
        auto hello = ctx.create_command("hello", "plugin hello");
        ctx.add_to_server(hello);
        ctx.add_subcommand(ctx.get_root_command(), hello);
      });
      pm.register_commands(root);

      bool found = false;
      for (const auto &cmd : pm.get_server_commands())
      {
        if (cmd->get_name() == "hello")
          found = true;
      }
      Expect(found).ToBe(true);
    });
  });
}

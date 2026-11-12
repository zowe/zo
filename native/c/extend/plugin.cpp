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

#ifndef _UNIX03_SOURCE
#define _UNIX03_SOURCE
#endif
#include <cerrno>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "plugin.hpp"
#include "../parser.hpp"
#include "../zlogger.hpp"

namespace
{
bool is_trusted_owner(uid_t owner)
{
  return owner == 0 || owner == geteuid();
}
} // namespace

namespace plugin
{
class RegistrationContextImpl
    : public CommandProviderImpl::CommandRegistrationContext
{

public:
  explicit RegistrationContextImpl(parser::Command &root)
      : m_root(root), m_root_record(root)
  {
    // Snapshot the built-in top-level tokens (name + aliases) a plug-in must not
    // shadow. All built-ins are registered before this runs.
    for (const auto &entry : m_root.get_commands())
    {
      m_builtin_names.insert(entry.first);
      if (entry.second)
      {
        for (const auto &alias : entry.second->get_aliases())
        {
          m_builtin_names.insert(alias);
        }
      }
    }
    m_claimed_names = m_builtin_names;
  }

  CommandHandle create_command(const char *name, const char *help)
  {
    std::string cmd_name = name ? name : "";
    std::string cmd_help = help ? help : "";

    parser::command_ptr command(new parser::Command(cmd_name, cmd_help));
    m_records.push_back(std::make_unique<CommandRecord>(command));
    return static_cast<CommandHandle>(m_records.back().get());
  }

  CommandHandle get_root_command()
  {
    return static_cast<CommandHandle>(&m_root_record);
  }

  void add_alias(CommandHandle command, const char *alias)
  {
    CommandRecord *record = to_record(command);
    if (record == nullptr || alias == nullptr)
      return;

    record->get().add_alias(alias);
  }

  void add_keyword_arg(CommandHandle command,
                       const char *name,
                       const char **aliases,
                       unsigned int alias_count,
                       const char *help,
                       ArgumentType type,
                       int required,
                       const DefaultValue *default_value)
  {
    CommandRecord *record = to_record(command);
    if (record == nullptr || name == nullptr)
      return;

    std::vector<std::string> alias_vector;
    if (aliases)
    {
      for (unsigned int i = 0; i < alias_count; ++i)
      {
        if (aliases[i])
          alias_vector.push_back(aliases[i]);
      }
    }

    parser::ArgValue defaultArg = convert_default(default_value);
    try
    {
      record->get().add_keyword_arg(std::string(name), alias_vector, std::string(help ? help : ""), convert_type(type), required != 0, defaultArg);
    }
    catch (const std::invalid_argument &e)
    {
      ZLOG_ERROR("Rejected plug-in keyword argument '%s': %s", name, e.what());
    }
  }

  void add_positional_arg(CommandHandle command,
                          const char *name,
                          const char *help,
                          ArgumentType type,
                          int required,
                          const DefaultValue *default_value)
  {
    CommandRecord *record = to_record(command);
    if (record == nullptr || name == nullptr)
      return;

    parser::ArgValue default_arg = convert_default(default_value);

    try
    {
      record->get().add_positional_arg(std::string(name),
                                       help ? std::string(help) : std::string(),
                                       parser::ArgType_Single, required != 0,
                                       default_arg);
    }
    catch (const std::invalid_argument &e)
    {
      ZLOG_ERROR("Rejected plug-in positional argument '%s': %s", name, e.what());
    }
  }

  void set_handler(CommandHandle command, CommandHandler handler)
  {
    CommandRecord *record = to_record(command);
    if (record == nullptr)
      return;

    record->get().set_handler(handler);
  }

  void add_subcommand(CommandHandle parent, CommandHandle child)
  {
    CommandRecord *parent_record = to_record(parent);
    CommandRecord *child_record = to_record(child);
    if (parent_record == nullptr || child_record == nullptr)
      return;

    if (parent_record->is_root())
    {
      // Defer wiring top-level commands into the live root until
      // commit_root_commands(), so the collision check sees each command's final
      // name/aliases and a rejected command never reaches add_command()'s throw.
      m_pending_root_commands.push_back(child_record->get_command_ptr());
    }
    else
    {
      // Nested subcommands can't shadow a built-in verb, but guard against a
      // malformed plug-in registering duplicate siblings so it can't abort zo.
      try
      {
        parent_record->get().add_command(child_record->get_command_ptr());
      }
      catch (const std::invalid_argument &e)
      {
        ZLOG_ERROR("Rejected plug-in subcommand: %s", e.what());
      }
    }
  }

  void add_to_server(CommandHandle command)
  {
    CommandRecord *record = to_record(command);
    if (record == nullptr)
      return;

    m_server_commands.insert(record->get_command_ptr());
  }

  const std::set<parser::command_ptr> &get_server_commands() const
  {
    return m_server_commands;
  }

  // Wire the deferred top-level commands into the root, refusing any whose name
  // or an alias would shadow a built-in verb (or a token another plug-in already
  // claimed). Rejected commands are logged and dropped from the server set too,
  // so a shadowing attempt gains no foothold on either path.
  void commit_root_commands()
  {
    for (const auto &command : m_pending_root_commands)
    {
      if (!command)
        continue;

      std::vector<std::string> tokens;
      tokens.push_back(command->get_name());
      const std::vector<std::string> &aliases = command->get_aliases();
      tokens.insert(tokens.end(), aliases.begin(), aliases.end());

      const TokenCollision collision = find_collision(tokens);
      if (collision.collided)
      {
        reject_pending_command(command, collision);
        continue;
      }

      try
      {
        m_root.add_command(command);
      }
      catch (const std::invalid_argument &e)
      {
        ZLOG_ERROR("Rejected plug-in command '%s': %s", command->get_name().c_str(), e.what());
        m_rejected_commands.push_back(command->get_name());
        m_server_commands.erase(command);
        continue;
      }

      for (const auto &token : tokens)
      {
        m_claimed_names.insert(token);
      }
    }
    m_pending_root_commands.clear();
  }

  const std::vector<std::string> &get_rejected_commands() const
  {
    return m_rejected_commands;
  }

private:
  struct TokenCollision
  {
    bool collided;
    bool shadows_builtin;
    std::string token;
  };

  TokenCollision find_collision(const std::vector<std::string> &tokens) const
  {
    for (const auto &token : tokens)
    {
      if (m_builtin_names.count(token) != 0)
      {
        return TokenCollision{true, true, token};
      }
      if (m_claimed_names.count(token) != 0)
      {
        return TokenCollision{true, false, token};
      }
    }
    return TokenCollision{false, false, std::string()};
  }

  void reject_pending_command(const parser::command_ptr &command, const TokenCollision &collision)
  {
    if (collision.shadows_builtin)
    {
      ZLOG_ERROR("Rejected plug-in command '%s': token '%s' would shadow a built-in zo command",
                 command->get_name().c_str(), collision.token.c_str());
    }
    else
    {
      ZLOG_ERROR("Rejected plug-in command '%s': token '%s' is already registered by another plug-in",
                 command->get_name().c_str(), collision.token.c_str());
    }
    m_rejected_commands.push_back(command->get_name());
    m_server_commands.erase(command);
  }

  struct CommandRecord
  {
    parser::command_ptr pointer;
    parser::Command *raw;
    bool root;

    explicit CommandRecord(parser::Command &existing)
        : raw(&existing), root(true)
    {
    }

    explicit CommandRecord(const parser::command_ptr &cmd)
        : pointer(cmd), raw(cmd.get()), root(false)
    {
    }

    parser::Command &get()
    {
      return *raw;
    }

    const parser::command_ptr &get_command_ptr() const
    {
      return pointer;
    }

    bool is_root() const
    {
      return root;
    }
  };

  CommandRecord *to_record(CommandHandle handle)
  {
    return static_cast<CommandRecord *>(handle);
  }

  parser::ArgValue convert_default(const DefaultValue *value)
  {
    if (value == nullptr || value->kind == DefaultValue::ValueKind_None)
    {
      return parser::ArgValue();
    }

    switch (value->kind)
    {
    case DefaultValue::ValueKind_Bool:
      return parser::ArgValue(value->value.bool_value != 0);
    case DefaultValue::ValueKind_Int:
      return parser::ArgValue(static_cast<long long>(value->value.int_value));
    case DefaultValue::ValueKind_Double:
      return parser::ArgValue(value->value.double_value);
    case DefaultValue::ValueKind_String:
      if (value->value.string_value)
        return parser::ArgValue(std::string(value->value.string_value));
      return parser::ArgValue();
    default:
      return parser::ArgValue();
    }
  }

  parser::ArgType convert_type(ArgumentType type)
  {
    switch (type)
    {
    case ArgumentType_Flag:
      return parser::ArgType_Flag;
    case ArgumentType_Single:
      return parser::ArgType_Single;
    case ArgumentType_Multiple:
      return parser::ArgType_Multiple;
    }
    return parser::ArgType_Flag;
  }

  parser::Command &m_root;
  CommandRecord m_root_record;
  std::vector<std::unique_ptr<CommandRecord>> m_records;
  std::set<parser::command_ptr> m_server_commands;
  std::set<std::string> m_builtin_names;
  std::set<std::string> m_claimed_names;
  std::vector<parser::command_ptr> m_pending_root_commands;
  std::vector<std::string> m_rejected_commands;
};

namespace
{
// Directory-level provenance check. Runs once, before any opendir/dlopen, and
// rejects the entire directory (loading nothing) when the plugins directory is
// not a safe place to load native code from. Cheaper than dlopen and
// dangerous-code-free, so it gates everything below.
bool validate_plugins_directory(const std::string &plugins_path, uid_t &out_dir_uid)
{
  struct stat dir_stat;
  if (stat(plugins_path.c_str(), &dir_stat) != 0)
  {
    ZLOG_ERROR("Refusing to load plugins: unable to stat plugins directory %s (errno %d)",
               plugins_path.c_str(), errno);
    return false;
  }

  if (!S_ISDIR(dir_stat.st_mode))
  {
    ZLOG_ERROR("Refusing to load plugins: %s is not a directory", plugins_path.c_str());
    return false;
  }

  if ((dir_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0)
  {
    ZLOG_ERROR("Refusing to load plugins: directory %s is group- or world-writable (mode %03o)", plugins_path.c_str(), static_cast<unsigned>(dir_stat.st_mode & 07777));
    return false;
  }

  if (!is_trusted_owner(dir_stat.st_uid))
  {
    ZLOG_ERROR("Refusing to load plugins: directory %s is owned by untrusted uid %u "
               "(expected root or the current user, uid %u)",
               plugins_path.c_str(), static_cast<unsigned>(dir_stat.st_uid),
               static_cast<unsigned>(geteuid()));
    return false;
  }

  out_dir_uid = dir_stat.st_uid;
  return true;
}

// Per-file provenance check. A plug-in dropped into an otherwise-trusted
// directory by a different, unprivileged identity is rejected even though the
// directory itself passed: the file owner must match the directory owner or be
// a trusted identity (root/self), and the file must not be group/world-writable.
bool should_load_plugin_entry(const std::string &plugin_path, uid_t dir_uid)
{
  // lstat (not stat) so a symlink is rejected on its own merits rather than followed to
  // whatever it currently points at
  struct stat entry_stat;
  if (lstat(plugin_path.c_str(), &entry_stat) != 0)
  {
    ZLOG_ERROR("Rejected plugin entry %s: unable to stat (errno %d)", plugin_path.c_str(), errno);
    return false;
  }

  if (!S_ISREG(entry_stat.st_mode))
  {
    ZLOG_ERROR("Rejected plugin entry %s: not a regular file", plugin_path.c_str());
    return false;
  }

  if (entry_stat.st_uid != dir_uid && !is_trusted_owner(entry_stat.st_uid))
  {
    ZLOG_ERROR("Rejected plugin entry %s: owned by untrusted uid %u (directory owner is uid %u)",
               plugin_path.c_str(), static_cast<unsigned>(entry_stat.st_uid),
               static_cast<unsigned>(dir_uid));
    return false;
  }

  if ((entry_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0)
  {
    ZLOG_ERROR("Rejected plugin entry %s: file is group- or world-writable (mode %03o)",
               plugin_path.c_str(), static_cast<unsigned>(entry_stat.st_mode & 07777));
    return false;
  }

  return true;
}
} // namespace

void PluginManager::load_plugin_file(const std::string &plugin_path, const std::string &entry_name)
{
  void *plugin = dlopen(plugin_path.c_str(), RTLD_LAZY);
  if (plugin == nullptr)
  {
    ZLOG_ERROR("Failed to open handle to plugin located at: %s", plugin_path.c_str());
    return;
  }

  // Check the ABI contract before calling into the plug-in: a plug-in built against a different
  // layout of the types that cross this boundary would otherwise corrupt memory silently.
  unsigned int (*abi_version)() = nullptr;
  *(void **)(&abi_version) = dlsym(plugin, "zo_plugin_abi_version");
  const unsigned int reported_abi = abi_version != nullptr ? abi_version() : 0u;
  if (reported_abi != ZO_PLUGIN_ABI_VERSION)
  {
    ZLOG_ERROR("Rejected plugin %s: reports plug-in ABI version %u, zowex requires %u. "
               "Rebuild the plug-in against the matching extend/plugin.hpp and expand "
               "ZO_PLUGIN_DECLARE_ABI() alongside register_plugin().",
               plugin_path.c_str(),
               reported_abi,
               ZO_PLUGIN_ABI_VERSION);
    dlclose(plugin);
    return;
  }

  void (*register_plugin)(plugin::PluginManager &) = nullptr;
  *(void **)(&register_plugin) = dlsym(plugin, "register_plugin");
  if (!register_plugin)
  {
    ZLOG_ERROR("Plugin %s loaded but no entrypoint available (register_plugin missing)", plugin_path.c_str());
    dlclose(plugin);
    return;
  }

  m_metadata_pending = false;
  m_pending_metadata = PluginMetadata();
  m_registration_rejected = false;
  m_duplicate_display_name.clear();
  m_provider_snapshot = m_command_providers.size();

  register_plugin(*this);

  if (m_registration_rejected)
  {
    ZLOG_ERROR("Plugin %s rejected because display name '%s' is already registered",
               plugin_path.c_str(),
               m_duplicate_display_name.c_str());
    discard_command_providers_from(m_provider_snapshot);
    dlclose(plugin);
    return;
  }

  record_loaded_plugin(plugin, entry_name);
}

void PluginManager::load_plugins(const std::string &plugins_path)
{
  m_plugins_path = plugins_path;

  uid_t dir_uid = 0;
  if (!validate_plugins_directory(m_plugins_path, dir_uid))
  {
    return;
  }

  auto *plugins_dir = opendir(m_plugins_path.c_str());
  if (plugins_dir == nullptr)
  {
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(plugins_dir)) != nullptr)
  {
    const std::string entry_name = entry->d_name;
    if (entry_name == "." || entry_name == "..")
    {
      ZLOG_DEBUG("Skipping plugin directory entry: %s", entry_name.c_str());
      continue;
    }

    const std::string plugin_path = m_plugins_path + "/" + entry_name;
    if (should_load_plugin_entry(plugin_path, dir_uid))
    {
      load_plugin_file(plugin_path, entry_name);
    }
  }
  closedir(plugins_dir);
}

void PluginManager::unload_plugins()
{
  m_command_providers.clear();
  m_server_commands.clear();
  for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it)
  {
    if (it->handle)
    {
      dlclose(it->handle);
      it->handle = nullptr;
    }
  }
}

void PluginManager::record_loaded_plugin(void *plugin_handle, const std::string &plugin_identifier)
{
  PluginMetadata metadata;
  if (m_metadata_pending)
  {
    metadata = m_pending_metadata;
  }
  else
  {
    metadata.display_name = plugin_identifier;
    metadata.version = "";
  }

  metadata.filename = plugin_identifier;

  m_plugins.push_back(LoadedPlugin(metadata, plugin_handle));
  m_metadata_pending = false;
  m_pending_metadata = PluginMetadata();
}

void PluginManager::register_commands(parser::Command &rootCommand)
{
  RegistrationContextImpl context(rootCommand);

  for (const auto &factory : m_command_providers)
  {
    if (!factory)
      continue;

    auto provider = factory->create();
    if (!provider)
      continue;

    provider->register_commands(context);
  }

  // Commit deferred top-level commands, rejecting any that would shadow a
  // built-in verb or collide with another plug-in.
  context.commit_root_commands();
  m_rejected_command_names = context.get_rejected_commands();

  for (const auto &command : context.get_server_commands())
  {
    m_server_commands.insert(command);
  }
}
} // namespace plugin

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

#include "uss.hpp"
#include "common_args.hpp"
#include "../parser.hpp"
#include "../zusf.hpp"
#include "../zut.hpp"

using namespace ast;
using namespace parser;
using namespace commands::common;

namespace uss
{

int handle_uss_copy(InvocationContext &context)
{
  std::string source_path = context.get<std::string>("source-path");
  std::string destination_path = context.get<std::string>("destination-path");

  bool recursive = context.get<bool>("recursive", false);
  bool follow_symlinks = context.get<bool>("follow-symlinks", false);
  bool preserve_attributes = context.get<bool>("preserve-attributes", false);
  bool force = context.get<bool>("force", false);

  const CopyOptions options(
      /* .recursive = */ recursive,
      /* .follow_symlinks = */ follow_symlinks,
      /* .preserve_attributes = */ preserve_attributes,
      /* .force = */ force);

  ZUSF zusf{};
  int rc = zusf_copy_file_or_dir(&zusf, source_path, destination_path, options);

  if (0 != rc)
  {
    context.error_stream() << "Error occurred while trying to copy \"" << source_path << "\" to \"" << destination_path << "\"" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
  }

  return rc;
}

int handle_uss_create_file(InvocationContext &context)
{
  int rc = 0;
  std::string file_path = context.get<std::string>("file-path", "");

  long long mode = context.get<long long>("mode", 644);
  bool overwrite = context.get<bool>("overwrite", false);

  if (mode == 0)
  {
    context.error_stream() << "Error: invalid mode provided.\nExamples of valid modes: 777, 0644" << std::endl;
    return RTNCD_FAILURE;
  }

  // Convert mode from decimal to octal
  mode_t cf_mode = 0;
  int temp_mode = mode;
  int multiplier = 1;

  // Convert decimal representation of octal to actual octal value
  // e.g. user inputs 777 -> converted to correct value for chmod
  while (temp_mode > 0)
  {
    cf_mode += (temp_mode % 10) * multiplier;
    temp_mode /= 10;
    multiplier *= 8;
  }

  ZUSF zusf{};
  rc = zusf_create_uss_file_or_dir(&zusf, file_path, cf_mode, CreateOptions(false, overwrite));
  if (RTNCD_WARNING == rc)
  {
    context.error_stream() << "Warning: " << zusf.diag.e_msg << std::endl;
    return RTNCD_WARNING;
  }
  else if (0 != rc)
  {
    context.error_stream() << "Error: could not create USS file: '" << file_path << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS file '" << file_path << "' created" << std::endl;

  return rc;
}

int handle_uss_create_dir(InvocationContext &context)
{
  int rc = 0;
  std::string file_path = context.get<std::string>("file-path", "");

  long long mode = context.get<long long>("mode", 755);

  if (mode == 0)
  {
    context.error_stream() << "Error: invalid mode provided.\nExamples of valid modes: 777, 0644" << std::endl;
    return RTNCD_FAILURE;
  }

  // Convert mode from decimal to octal
  mode_t cf_mode = 0;
  int temp_mode = mode;
  int multiplier = 1;

  // Convert decimal representation of octal to actual octal value
  // e.g. user inputs 777 -> converted to correct value for chmod
  while (temp_mode > 0)
  {
    cf_mode += (temp_mode % 10) * multiplier;
    temp_mode /= 10;
    multiplier *= 8;
  }

  ZUSF zusf{};
  rc = zusf_create_uss_file_or_dir(&zusf, file_path, cf_mode, CreateOptions(true));
  if (0 != rc)
  {
    context.error_stream() << "Error: could not create USS directory: '" << file_path << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS directory '" << file_path << "' created" << std::endl;

  return rc;
}

int handle_uss_move(InvocationContext &context)
{
  std::string source = context.get<std::string>("source", "");
  std::string target = context.get<std::string>("target", "");
  bool force = context.get<bool>("force", true);

  ZUSF zusf{};
  int rc = zusf_move_uss_file_or_dir(&zusf, source, target, force);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not move USS file or directory: '" << source << "' to '" << target << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }
  context.output_stream() << "USS file or directory '" << source << "' moved to '" << target << "'" << std::endl;
  return rc;
}

int handle_uss_list(InvocationContext &context)
{
  int rc = 0;
  std::string uss_file = context.get<std::string>("file-path", "");

  ListOptions list_options{};
  list_options.all_files = context.get<bool>("all", false);
  list_options.long_format = context.get<bool>("long", false);
  list_options.max_depth = context.get<long long>("depth", 1);

  const auto use_csv_format = context.get<bool>("response-format-csv", false);

  ZUSF zusf{};
  std::string response;
  std::vector<ZusfListEntry> entries;
  rc = zusf_list_uss_file_path(&zusf, uss_file, response, list_options, use_csv_format, &entries);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not list USS files: '" << uss_file << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details:\n"
                           << zusf.diag.e_msg << std::endl
                           << response << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << response;

  const auto result = obj();
  const auto entries_array = arr();

  for (const auto &list_entry : entries)
  {
    const auto entry = obj();

    if (list_options.long_format)
    {
      entry->set("mode", str(list_entry.mode));
      entry->set("links", i64(list_entry.links));
      entry->set("user", str(list_entry.user));
      entry->set("group", str(list_entry.group));
      entry->set("size", i64(list_entry.size));
      entry->set("filetag", str(list_entry.filetag));
      entry->set("mtime", str(list_entry.mtime));
      entry->set("name", str(list_entry.name));
    }
    else
    {
      // Simple format: just the name
      entry->set("name", str(list_entry.name));
    }

    entries_array->push(entry);
  }

  result->set("items", entries_array);
  result->set("returnedRows", i64(entries.size()));
  context.set_object(result);

  return rc;
}

int handle_uss_view(InvocationContext &context)
{
  int rc = 0;
  std::string uss_file = context.get<std::string>("file-path", "");

  ZUSF zusf{};
  if (context.has("encoding"))
  {
    zut_prepare_encoding(context.get<std::string>("encoding", ""), &zusf.encoding_opts);
  }
  if (context.has("local-encoding"))
  {
    const auto source_encoding = context.get<std::string>("local-encoding", "");
    if (!source_encoding.empty() && source_encoding.size() < sizeof(zusf.encoding_opts.source_codepage))
    {
      memcpy(zusf.encoding_opts.source_codepage, source_encoding.data(), source_encoding.length() + 1);
    }
  }

  struct stat file_stats;
  if (stat(uss_file.c_str(), &file_stats) == -1)
  {
    context.error_stream() << "Error: Path " << uss_file << " does not exist" << std::endl;
    return RTNCD_FAILURE;
  }

  bool has_pipe_path = context.has("pipe-path");
  std::string pipe_path = context.get<std::string>("pipe-path", "");
  const auto result = obj();

  if (has_pipe_path && !pipe_path.empty())
  {
    // Set up callback for content length reporting
    zusf.set_size_callback = [&context](uint64_t size)
    {
      context.set_content_len(size);
    };
    zusf.update_heartbeat_callback = [&context]()
    {
      context.update_heartbeat();
    };

    size_t content_len = 0;
    rc = zusf_read_from_uss_file_streamed(&zusf, uss_file, pipe_path, &content_len);

    if (context.get<bool>("return-etag", false))
    {
      const auto etag = zut_build_etag(file_stats.st_mtime, file_stats.st_size);
      if (!context.is_redirecting_output())
      {
        context.output_stream() << "etag: " << etag << std::endl;
      }
      result->set("etag", str(etag));
    }

    if (!context.is_redirecting_output())
    {
      context.output_stream() << "size: " << content_len << std::endl;
    }
    result->set("contentLen", i64(content_len));
  }
  else
  {
    std::string response;
    rc = zusf_read_from_uss_file(&zusf, uss_file, response);
    if (0 != rc)
    {
      context.error_stream() << "Error: could not view USS file: '" << uss_file << "' rc: '" << rc << "'" << std::endl;
      context.error_stream() << "  Details:\n"
                             << zusf.diag.e_msg << std::endl
                             << response << std::endl;
      return RTNCD_FAILURE;
    }

    if (context.get<bool>("return-etag", false))
    {
      const auto etag = zut_build_etag(file_stats.st_mtime, file_stats.st_size);
      if (!context.is_redirecting_output())
      {
        context.output_stream() << "etag: " << etag << std::endl;
        context.output_stream() << "data: ";
      }
      result->set("etag", str(etag));
    }

    bool has_encoding = context.has("encoding");
    bool response_format_bytes = context.get<bool>("response-format-bytes", false);

    if (has_encoding && response_format_bytes)
    {
      zut_print_string_as_bytes(response, &context.output_stream());
    }
    else
    {
      context.output_stream() << response;
    }
  }

  context.set_object(result);

  return rc;
}

int handle_uss_write(InvocationContext &context)
{
  int rc = 0;
  std::string file = context.get<std::string>("file-path", "");
  ZUSF zusf{};

  if (context.has("encoding"))
  {
    zut_prepare_encoding(context.get<std::string>("encoding", ""), &zusf.encoding_opts);
  }
  if (context.has("local-encoding"))
  {
    const auto source_encoding = context.get<std::string>("local-encoding", "");
    if (!source_encoding.empty() && source_encoding.size() < sizeof(zusf.encoding_opts.source_codepage))
    {
      memcpy(zusf.encoding_opts.source_codepage, source_encoding.data(), source_encoding.length() + 1);
    }
  }

  if (context.has("etag"))
  {
    std::string etag_value = context.get<std::string>("etag", "");
    if (!etag_value.empty())
    {
      if (etag_value.size() >= sizeof(zusf.etag))
      {
        context.error_stream() << "Error: etag exceeds " << sizeof(zusf.etag) - 1 << " character length limit" << std::endl;
        return RTNCD_FAILURE;
      }
      memcpy(zusf.etag, etag_value.c_str(), etag_value.size() + 1);
    }
  }

  bool has_pipe_path = context.has("pipe-path");
  std::string pipe_path = context.get<std::string>("pipe-path", "");
  size_t content_len = 0;
  const auto result = obj();

  if (has_pipe_path && !pipe_path.empty())
  {
    zusf.update_heartbeat_callback = [&context]()
    {
      context.update_heartbeat();
    };

    rc = zusf_write_to_uss_file_streamed(&zusf, file, pipe_path, &content_len);
    result->set("contentLen", i64(content_len));
  }
  else
  {
    std::string data = zut_read_input(context.input_stream());
    rc = zusf_write_to_uss_file(&zusf, file, data);
  }

  if (0 != rc)
  {
    context.error_stream() << "Error: could not write to USS file: '" << file << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  if (context.get<bool>("etag-only", false))
  {
    context.output_stream() << "etag: " << zusf.etag << std::endl
                            << "created: " << (zusf.created ? "true" : "false") << std::endl;
    if (content_len > 0)
      context.output_stream() << "size: " << content_len << std::endl;
  }
  else
  {
    context.output_stream() << "Wrote data to '" << file << "'" << (zusf.created ? " (created new file)" : " (overwrote existing)") << std::endl;
  }

  result->set("created", boolean(zusf.created));
  result->set("etag", str(zusf.etag));
  context.set_object(result);

  return rc;
}

int handle_uss_delete(InvocationContext &context)
{
  std::string file_path = context.get<std::string>("file-path", "");
  bool recursive = context.get<bool>("recursive", false);

  ZUSF zusf{};
  const auto rc = zusf_delete_uss_item(&zusf, file_path, recursive);

  if (0 != rc)
  {
    context.error_stream() << "Failed to delete USS item " << file_path << ":\n " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS item '" << file_path << "' deleted" << std::endl;

  return rc;
}

int handle_uss_chmod(InvocationContext &context)
{
  int rc = 0;
  long long mode = context.get<long long>("mode", 0);
  if (mode == 0 && !context.get<std::string>("mode", "").empty())
  {
    std::string mode_str = context.get<std::string>("mode", "");
    if (mode_str.find_first_not_of("01234567") != std::string::npos || mode_str.length() < 3 || mode_str.length() > 4) {
      context.error_stream() << "Error: invalid octal mode provided. Examples of valid modes: 777, 0644" << std::endl;
      return RTNCD_FAILURE;
    }
    try
    {
      mode = std::stoll(mode_str);
    }
    catch (...)
    {
      context.error_stream() << "Error: invalid mode provided.\nExamples of valid modes: 777, 0644" << std::endl;
      return RTNCD_FAILURE;
    }
  }

  std::string file_path = context.get<std::string>("file-path", "");
  bool recursive = context.get<bool>("recursive", false);

  // Convert mode from decimal to octal
  mode_t chmod_mode = 0;
  int temp_mode = mode;
  int multiplier = 1;

  // Convert decimal representation of octal to actual octal value
  // e.g. user inputs 777 -> converted to correct value for chmod
  while (temp_mode > 0)
  {
    chmod_mode += (temp_mode % 10) * multiplier;
    temp_mode /= 10;
    multiplier *= 8;
  }

  ZUSF zusf{};
  rc = zusf_chmod_uss_file_or_dir(&zusf, file_path, chmod_mode, recursive);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not chmod USS path: '" << file_path << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS path '" << file_path << "' modified: '" << mode << "'" << std::endl;

  return rc;
}

int handle_uss_chown(InvocationContext &context)
{
  std::string path = context.get<std::string>("file-path", "");
  std::string owner = context.get<std::string>("owner", "");
  bool recursive = context.get<bool>("recursive", false);

  ZUSF zusf{};

  const auto rc = zusf_chown_uss_file_or_dir(&zusf, path, owner, recursive);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not chown USS path: '" << path << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS path '" << path << "' owner changed to '" << owner << "'" << std::endl;

  return rc;
}

int handle_uss_chtag(InvocationContext &context)
{
  std::string path = context.get<std::string>("file-path", "");
  std::string tag = context.get<std::string>("tag", "");
  if (tag.empty())
  {
    tag = std::to_string(context.get<long long>("tag", 0));
  }

  if (tag.empty())
  {
    context.error_stream() << "Error: no tag provided" << std::endl;
    return RTNCD_FAILURE;
  }

  bool recursive = context.get<bool>("recursive", false);

  ZUSF zusf{};
  const auto rc = zusf_chtag_uss_file_or_dir(&zusf, path, tag, recursive);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not chtag USS path: '" << path << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zusf.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "USS path '" << path << "' tag changed to '" << tag << "'" << std::endl;

  return rc;
}

int handle_uss_issue_cmd(InvocationContext &context)
{
  std::string command = context.get<std::string>("command", "");
  std::string stdout_response;
  std::string stderr_response;

  // Command output may differ from an interactive shell session: the child
  // process receives a pipe as stdout (not a TTY), so programs that check
  // isatty(1) to control formatting (e.g. `ls` multi-column layout, `grep`
  // color) will use their non-interactive defaults. This is expected behavior.
  int rc = zut_spawn_shell_command(command, stdout_response, stderr_response);

  if (0 != rc)
  {
    context.error_stream() << "Error running command, rc '" << rc << "'" << std::endl;
    if (!stderr_response.empty())
    {
      context.error_stream() << "  Details: " << stderr_response << std::endl;
    }
  }
  else if (!stderr_response.empty())
  {
    const auto result = ast::obj();
    result->set("stderr", ast::str(stderr_response));
    context.set_object(result);
  }

  context.output_stream() << stdout_response << '\n';

  return rc;
}

void register_commands(parser::Command &root_command)
{
  // USS command group
  auto uss_group = command_ptr(new Command("uss", "z/OS USS operations"));

  // Copy subcommand
  auto uss_copy_cmd = command_ptr(new Command("copy", "copy a USS file or directory"));
  uss_copy_cmd->add_positional_arg(FILE_PATH_SOURCE);
  uss_copy_cmd->add_positional_arg(FILE_PATH_DEST);
  uss_copy_cmd->add_keyword_arg("follow-symlinks", make_aliases("--follow-symlinks", "-L"), "follows symlinks within a directory tree. requires \"--recursive\"", ArgType_Flag, false);
  uss_copy_cmd->add_keyword_arg("recursive", make_aliases("--recursive", "-r"), "recursively copies if the source is a directory", ArgType_Flag, false);
  uss_copy_cmd->add_keyword_arg("preserve-attributes", make_aliases("--preserve-attributes", "-p"), "preserve permission bits and ownership on copy to destination", ArgType_Flag, false);
  uss_copy_cmd->add_keyword_arg("force", make_aliases("--force", "-f"), "attempts to remove and replace a UNIX destination file that cannot be opened", ArgType_Flag, false);
  uss_copy_cmd->set_handler(handle_uss_copy);
  uss_group->add_command(uss_copy_cmd);

  // Create-file subcommand
  auto uss_create_file_cmd = command_ptr(new Command("create-file", "create a USS file"));
  uss_create_file_cmd->add_positional_arg(FILE_PATH);
  uss_create_file_cmd->add_keyword_arg("mode", make_aliases("--mode"), "permissions", ArgType_Single, false);
  uss_create_file_cmd->add_keyword_arg("overwrite", make_aliases("--overwrite", "--ow"), "overwrite existing file", ArgType_Flag, false, ArgValue(false));
  uss_create_file_cmd->set_handler(handle_uss_create_file);
  uss_group->add_command(uss_create_file_cmd);

  // Create-dir subcommand
  auto uss_create_dir_cmd = command_ptr(new Command("create-dir", "create a USS directory"));
  uss_create_dir_cmd->add_positional_arg(FILE_PATH);
  uss_create_dir_cmd->add_keyword_arg("mode", make_aliases("--mode"), "permissions", ArgType_Single, false);
  uss_create_dir_cmd->set_handler(handle_uss_create_dir);
  uss_group->add_command(uss_create_dir_cmd);

  // Move subcommand
  auto uss_move_cmd = command_ptr(new Command("move", "move a USS file or directory"));
  uss_move_cmd->add_alias("mv");
  uss_move_cmd->add_positional_arg("source", "source path", ArgType_Single, true);
  uss_move_cmd->add_positional_arg("target", "target path", ArgType_Single, true);
  uss_move_cmd->add_arg("force", make_aliases(), "force overwrite", ArgType_Flag, false, false, ArgValue(true), true);
  uss_move_cmd->set_handler(handle_uss_move);
  uss_group->add_command(uss_move_cmd);

  // List subcommand
  auto uss_list_cmd = command_ptr(new Command("list", "list USS files and directories"));
  uss_list_cmd->add_alias("ls");
  uss_list_cmd->add_positional_arg(FILE_PATH);
  uss_list_cmd->add_keyword_arg("all", make_aliases("--all", "-a"), "list all files and directories", ArgType_Flag, false, ArgValue(false));
  uss_list_cmd->add_keyword_arg("long", make_aliases("--long", "-l"), "list long format", ArgType_Flag, false, ArgValue(false));
  uss_list_cmd->add_keyword_arg("depth", make_aliases("--depth"), "depth of subdirectories to list", ArgType_Single, false, ArgValue((long long)1));
  uss_list_cmd->add_keyword_arg(RESPONSE_FORMAT_CSV);
  uss_list_cmd->set_handler(handle_uss_list);
  uss_group->add_command(uss_list_cmd);

  // View subcommand
  auto uss_view_cmd = command_ptr(new Command("view", "view a USS file"));
  uss_view_cmd->mark_stdout_as_payload();
  uss_view_cmd->add_positional_arg(FILE_PATH);
  uss_view_cmd->add_keyword_arg(ENCODING);
  uss_view_cmd->add_keyword_arg(LOCAL_ENCODING);
  uss_view_cmd->add_keyword_arg(RESPONSE_FORMAT_BYTES);
  uss_view_cmd->add_keyword_arg(RETURN_ETAG);
  uss_view_cmd->add_keyword_arg(PIPE_PATH);
  uss_view_cmd->set_handler(handle_uss_view);
  uss_group->add_command(uss_view_cmd);

  // Write subcommand
  auto uss_write_cmd = command_ptr(new Command("write", "write to a USS file"));
  uss_write_cmd->add_positional_arg(FILE_PATH);
  uss_write_cmd->add_keyword_arg(ENCODING_INPUT);
  uss_write_cmd->add_keyword_arg(LOCAL_ENCODING);
  uss_write_cmd->add_keyword_arg(ETAG);
  uss_write_cmd->add_keyword_arg(ETAG_ONLY);
  uss_write_cmd->add_keyword_arg(PIPE_PATH);
  uss_write_cmd->set_handler(handle_uss_write);
  uss_group->add_command(uss_write_cmd);

  // Delete subcommand
  auto uss_delete_cmd = command_ptr(new Command("delete", "delete a USS item"));
  uss_delete_cmd->add_positional_arg(FILE_PATH);
  uss_delete_cmd->add_keyword_arg(RECURSIVE);
  uss_delete_cmd->set_handler(handle_uss_delete);
  uss_group->add_command(uss_delete_cmd);

  // Chmod subcommand
  auto uss_chmod_cmd = command_ptr(new Command("chmod", "change permissions on a USS file or directory"));
  uss_chmod_cmd->add_positional_arg("mode", "new permissions for the file or directory", ArgType_Single, true);
  uss_chmod_cmd->add_positional_arg(FILE_PATH);
  uss_chmod_cmd->add_keyword_arg(RECURSIVE);
  uss_chmod_cmd->set_handler(handle_uss_chmod);
  uss_group->add_command(uss_chmod_cmd);

  // Chown subcommand
  auto uss_chown_cmd = command_ptr(new Command("chown", "change owner on a USS file or directory"));
  uss_chown_cmd->add_positional_arg("owner", "New owner (or owner:group) for the file or directory", ArgType_Single, true);
  uss_chown_cmd->add_positional_arg(FILE_PATH);
  uss_chown_cmd->add_keyword_arg(RECURSIVE);
  uss_chown_cmd->set_handler(handle_uss_chown);
  uss_group->add_command(uss_chown_cmd);

  // Chtag subcommand
  auto uss_chtag_cmd = command_ptr(new Command("chtag", "change tags on a USS file"));
  uss_chtag_cmd->add_positional_arg(FILE_PATH);
  uss_chtag_cmd->add_positional_arg("tag", "new tag for the file", ArgType_Single, true);
  uss_chtag_cmd->add_keyword_arg(RECURSIVE);
  uss_chtag_cmd->set_handler(handle_uss_chtag);
  uss_group->add_command(uss_chtag_cmd);

  // Issue subcommand
  auto uss_issue_cmd = std::make_shared<Command>("issue", "issue a UNIX command");
  uss_issue_cmd->mark_stdout_as_payload();
  uss_issue_cmd->add_positional_arg("command", "command to issue", ArgType_Single, true);
  uss_issue_cmd->set_handler(handle_uss_issue_cmd);
  uss_group->add_command(uss_issue_cmd);

  root_command.add_command(uss_group);
}
} // namespace uss

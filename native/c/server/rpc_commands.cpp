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

#include "rpc_commands.hpp"
#include "dispatcher.hpp"
#include <cstdlib>
#include <unistd.h>
#include "schemas/requests.hpp"
#include "schemas/responses.hpp"
#include "../commands/certificates.hpp"
#include "../commands/core.hpp"
#include "../commands/ds.hpp"
#include "../commands/job.hpp"
#include "../commands/server.hpp"
#include "../commands/system.hpp"
#include "../commands/tso.hpp"
#include "../commands/uss.hpp"
#include "../commands/tool.hpp"
#include "../zut.hpp"
#include "../ztype.h"

// Helper functions to create builders with common argument mappings
static CommandBuilder create_ds_builder(CommandBuilder::CommandHandler handler)
{
  return CommandBuilder(handler).rename_arg("dsname", "dsn");
}

static CommandBuilder create_job_builder(CommandBuilder::CommandHandler handler)
{
  return CommandBuilder(handler).rename_arg("jobId", "jobid");
}

static CommandBuilder create_uss_builder(CommandBuilder::CommandHandler handler)
{
  return CommandBuilder(handler).rename_arg("fspath", "file-path");
}

static CommandBuilder copy_uss_builder(CommandBuilder::CommandHandler handler)
{
  return CommandBuilder(handler).rename_arg("srcFsPath", "source-path").rename_arg("dstFsPath", "destination-path");
}

void register_ds_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("createDataset",
                              create_ds_builder(ds::create_with_attributes)
                                  .validate<CreateDatasetRequest, CreateDatasetResponse>()
                                  .flatten_obj("attributes"));
  dispatcher.register_command("createMember",
                              create_ds_builder(ds::handle_data_set_create_member)
                                  .validate<CreateMemberRequest, CreateMemberResponse>());
  dispatcher.register_command("deleteDataset",
                              create_ds_builder(ds::handle_data_set_delete)
                                  .validate<DeleteDatasetRequest, DeleteDatasetResponse>());
  dispatcher.register_command("listDatasets",
                              CommandBuilder(ds::handle_data_set_list)
                                  .validate<ListDatasetsRequest, ListDatasetsResponse>()
                                  .rename_arg("pattern", "dsn")
                                  .rename_arg("maxItems", "max-entries")
                                  .set_default("warn", false));
  dispatcher.register_command("listDsMembers",
                              create_ds_builder(ds::handle_data_set_list_members)
                                  .validate<ListDsMembersRequest, ListDsMembersResponse>()
                                  .rename_arg("maxItems", "max-entries")
                                  .set_default("warn", false)
                                  .set_default("pattern", ""));
  dispatcher.register_command("resolveDsAlias",
                              create_ds_builder(ds::handle_data_set_resolve_alias)
                                  .validate<ResolveDsAliasRequest, ResolveDsAliasResponse>());
  dispatcher.register_command("readDataset",
                              create_ds_builder(ds::handle_data_set_view)
                                  .validate<ReadDatasetRequest, ReadDatasetResponse>()
                                  .rename_arg("volume", "volser")
                                  .set_default("encoding", "IBM-1047")
                                  .set_default("return-etag", true)
                                  .read_stdout("data", true)
                                  .handle_fifo("stream", "pipe-path", FifoMode::GET));
  dispatcher.register_command("restoreDataset",
                              create_ds_builder(ds::handle_data_set_restore)
                                  .validate<RestoreDatasetRequest, RestoreDatasetResponse>());
  dispatcher.register_command("writeDataset",
                              create_ds_builder(ds::handle_data_set_write)
                                  .validate<WriteDatasetRequest, WriteDatasetResponse>()
                                  .rename_arg("volume", "volser")
                                  .set_default("encoding", "IBM-1047")
                                  .write_stdin("data", true)
                                  .handle_fifo("stream", "pipe-path", FifoMode::PUT));
  dispatcher.register_command("renameDataset", create_ds_builder(ds::handle_data_set_rename).validate<RenameDatasetRequest, RenameDatasetResponse>());
  dispatcher.register_command("renameMember", create_ds_builder(ds::handle_rename_member).validate<RenameMemberRequest, RenameMemberResponse>());
  dispatcher.register_command("copyDatasetOrMember",
                              CommandBuilder(ds::handle_data_set_copy)
                                  .validate<CopyDatasetRequest, CopyDatasetResponse>());
}

void register_job_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("cancelJob",
                              create_job_builder(job::handle_job_cancel)
                                  .validate<CancelJobRequest, CancelJobResponse>());
  dispatcher.register_command("deleteJob",
                              create_job_builder(job::handle_job_delete)
                                  .validate<DeleteJobRequest, DeleteJobResponse>());
  dispatcher.register_command("getJcl",
                              create_job_builder(job::handle_job_view_jcl)
                                  .validate<GetJclRequest, GetJclResponse>()
                                  .read_stdout("data", false));
  dispatcher.register_command("getJobStatus",
                              create_job_builder(job::handle_job_view_status)
                                  .validate<GetJobStatusRequest, GetJobStatusResponse>());
  dispatcher.register_command("holdJob",
                              create_job_builder(job::handle_job_hold)
                                  .validate<HoldJobRequest, HoldJobResponse>());
  dispatcher.register_command("listJobs",
                              CommandBuilder(job::handle_job_list)
                                  .validate<ListJobsRequest, ListJobsResponse>()
                                  .rename_arg("maxItems", "max-entries")
                                  .set_default("warn", false));
  dispatcher.register_command("listSpools",
                              create_job_builder(job::handle_job_list_files)
                                  .validate<ListSpoolsRequest, ListSpoolsResponse>());
  dispatcher.register_command("readSpool",
                              create_job_builder(job::handle_job_view_file_by_id)
                                  .validate<ReadSpoolRequest, ReadSpoolResponse>()
                                  .rename_arg("spoolId", "key")
                                  .set_default("encoding", "IBM-1047")
                                  .read_stdout("data", true));
  dispatcher.register_command("releaseJob",
                              create_job_builder(job::handle_job_release)
                                  .validate<ReleaseJobRequest, ReleaseJobResponse>());
  dispatcher.register_command("submitJcl",
                              CommandBuilder(job::handle_job_submit_jcl)
                                  .validate<SubmitJclRequest, SubmitJclResponse>()
                                  .set_default("encoding", "IBM-1047")
                                  .write_stdin("jcl", true));
  dispatcher.register_command("submitJob",
                              create_ds_builder(job::handle_job_submit)
                                  .validate<SubmitJobRequest, SubmitJclResponse>());
  dispatcher.register_command("submitUss",
                              create_uss_builder(job::handle_job_submit_uss)
                                  .validate<SubmitUssRequest, SubmitJclResponse>());
}

void register_uss_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("chmodFile",
                              create_uss_builder(uss::handle_uss_chmod)
                                  .validate<ChmodFileRequest, ChmodFileResponse>());
  dispatcher.register_command("chownFile",
                              create_uss_builder(uss::handle_uss_chown)
                                  .validate<ChownFileRequest, ChownFileResponse>());
  dispatcher.register_command("chtagFile",
                              create_uss_builder(uss::handle_uss_chtag)
                                  .validate<ChtagFileRequest, ChtagFileResponse>());
  dispatcher.register_command("copyUss", copy_uss_builder(uss::handle_uss_copy).validate<CopyUssRequest, CopyUssResponse>());
  const auto handle_uss_create = [](plugin::InvocationContext &context) -> int
  {
    auto handler = context.get<bool>("is-dir", false) ?
      uss::handle_uss_create_dir : uss::handle_uss_create_file;
    return handler(context); };
  dispatcher.register_command("createFile",
                              create_uss_builder(handle_uss_create)
                                  .validate<CreateFileRequest, CreateFileResponse>()
                                  .rename_arg("permissions", "mode"));
  dispatcher.register_command("deleteFile",
                              create_uss_builder(uss::handle_uss_delete)
                                  .validate<DeleteFileRequest, DeleteFileResponse>());
  dispatcher.register_command("moveFile",
                              create_uss_builder(uss::handle_uss_move)
                                  .validate<MoveFileRequest, MoveFileResponse>()
                                  .set_default("force", true));
  dispatcher.register_command("listFiles",
                              create_uss_builder(uss::handle_uss_list)
                                  .validate<ListFilesRequest, ListFilesResponse>()
                                  .set_default("response-format-csv", true));
  dispatcher.register_command("readFile",
                              create_uss_builder(uss::handle_uss_view)
                                  .validate<ReadFileRequest, ReadFileResponse>()
                                  .set_default("encoding", "IBM-1047")
                                  .set_default("return-etag", true)
                                  .read_stdout("data", true)
                                  .handle_fifo("stream", "pipe-path", FifoMode::GET, true));
  dispatcher.register_command("writeFile",
                              create_uss_builder(uss::handle_uss_write)
                                  .validate<WriteFileRequest, WriteFileResponse>()
                                  .set_default("encoding", "IBM-1047")
                                  .write_stdin("data", true)
                                  .handle_fifo("stream", "pipe-path", FifoMode::PUT));
  dispatcher.register_command("unixCommand",
                              CommandBuilder(uss::handle_uss_issue_cmd)
                                  .validate<IssueUssCmdRequest, IssueUssCmdResponse>()
                                  .rename_arg("commandText", "command")
                                  .read_stdout("data", false));
}

void register_tso_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("tsoCommand",
                              CommandBuilder(tso::handle_tso_issue)
                                  .validate<IssueTsoCmdRequest, IssueTsoCmdResponse>()
                                  .rename_arg("commandText", "command")
                                  .read_stdout("data", false));
}

static std::string locate_zoa()
{
  const char *override_path = getenv("ZOA_PATH");
  if (override_path != nullptr && *override_path != '\0')
  {
    return override_path;
  }
  const std::string &exec_dir = ZServer::get_instance().get_exec_dir();
  if (!exec_dir.empty())
  {
    const std::string sibling = exec_dir + "/zoa";
    if (0 == access(sibling.c_str(), X_OK))
    {
      return sibling;
    }
  }
  return "zoa";
}

static int handle_console_command(plugin::InvocationContext &context)
{
  const std::string command = context.get<std::string>("command", "");
  std::string console_name = context.get<std::string>("console-name", "");
  const long long timeout = context.get<long long>("timeout", 0);
  const bool wait = context.get<bool>("wait", true);

  std::vector<std::string> args;
  args.emplace_back("console");
  args.emplace_back("issue");
  args.emplace_back(command);
  if (!console_name.empty())
  {
    args.emplace_back("--console-name");
    args.emplace_back(console_name);
  }
  if (timeout > 0)
  {
    args.emplace_back("--timeout");
    args.emplace_back(std::to_string(timeout));
  }
  args.emplace_back("--wait");
  args.emplace_back(wait ? "true" : "false");

  std::string out;
  std::string err;
  const std::string zoa = locate_zoa();
  const int rc = zut_run_program(zoa, args, out, err);

  if (0 != rc)
  {
    const std::string &details = err.empty() ? out : err;
    context.error_stream() << "Error: console command failed via '" << zoa << "', rc: '" << rc << "'" << std::endl;
    if (!details.empty())
    {
      context.error_stream() << "  Details: " << details << std::endl;
    }
    if (std::string::npos != details.find("command not found") ||
        std::string::npos != details.find("Permission denied"))
    {
      context.error_stream() << "  The zoa binary must be installed alongside zo (or named via ZOA_PATH) and be executable by this user." << std::endl;
    }
    else if (std::string::npos != details.find("Not authorized"))
    {
      context.error_stream() << "  The zoa binary is not APF-authorized; a system programmer must authorize it (extattr +ap) in a controlled location." << std::endl;
    }
    return RTNCD_FAILURE;
  }

  context.output_stream() << out;
  return RTNCD_SUCCESS;
}

void register_console_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("consoleCommand",
                              CommandBuilder(handle_console_command)
                                  .validate<IssueConsoleCmdRequest, IssueConsoleCmdResponse>()
                                  .rename_arg("commandText", "command")
                                  .read_stdout("data", false));
}

void register_tool_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("toolSearch",
                              create_ds_builder(tool::handle_tool_search)
                                  .validate<ToolSearchRequest, ToolSearchResponse>()
                                  .read_stdout("data", false));
}

void register_core_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("getInfo",
                              CommandBuilder(core::handle_version)
                                  .validate<GetInfoRequest, GetInfoResponse>());
}

void register_system_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("viewSyslog",
                              CommandBuilder(sys::handle_system_view_syslog)
                                  .validate<ViewSyslogRequest, ViewSyslogResponse>()
                                  .rename_arg("secondsAgo", "seconds-ago")
                                  .rename_arg("maxLines", "max-lines")
                                  .read_stdout("data", false));
  dispatcher.register_command("listParmlib",
                              CommandBuilder(sys::handle_system_list_parmlib)
                                  .validate<ListParmlibRequest, ListParmlibResponse>());
  dispatcher.register_command("listProclib",
                              CommandBuilder(sys::handle_system_list_proclib)
                                  .validate<ListProclibRequest, ListProclibResponse>());
  dispatcher.register_command("listApf",
                              CommandBuilder(sys::handle_system_list_apf)
                                  .validate<ListApfRequest, ListApfResponse>());
  dispatcher.register_command("listLinklist",
                              CommandBuilder(sys::handle_system_list_linklist)
                                  .validate<ListLinklistRequest, ListLinklistResponse>());
}

void register_certificates_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("createKeyring",
                              CommandBuilder(certificates::handle_create_ring)
                                  .validate<CreateKeyringRequest, CreateKeyringResponse>());
  dispatcher.register_command("deleteKeyring",
                              CommandBuilder(certificates::handle_delete_ring)
                                  .validate<DeleteKeyringRequest, DeleteKeyringResponse>());
  dispatcher.register_command("refreshDigtcert",
                              CommandBuilder(certificates::handle_refresh)
                                  .validate<RefreshDigtcertRequest, RefreshDigtcertResponse>());
  dispatcher.register_command("deleteCertificate",
                              CommandBuilder(certificates::handle_cert_delete)
                                  .validate<DeleteCertificateRequest, DeleteCertificateResponse>());
  dispatcher.register_command("listCertificates",
                              CommandBuilder(certificates::handle_cert_list)
                                  .validate<ListCertificatesRequest, ListCertificatesResponse>());
  dispatcher.register_command("exportCertificate",
                              CommandBuilder(certificates::handle_cert_export)
                                  .validate<ExportCertificateRequest, ExportCertificateResponse>());
  dispatcher.register_command("importCertificate",
                              CommandBuilder(certificates::handle_cert_import)
                                  .validate<ImportCertificateRequest, ImportCertificateResponse>());
  dispatcher.register_command("showCertificate",
                              CommandBuilder(certificates::handle_cert_show)
                                  .validate<ShowCertificateRequest, ShowCertificateResponse>());
  dispatcher.register_command("listRings",
                              CommandBuilder(certificates::handle_list_rings)
                                  .validate<ListRingsRequest, ListRingsResponse>());
  dispatcher.register_command("countRing",
                              CommandBuilder(certificates::handle_ring_count)
                                  .validate<CountRingRequest, CountRingResponse>());
  dispatcher.register_command("connectCertificate",
                              CommandBuilder(certificates::handle_cert_connect)
                                  .validate<ConnectCertificateRequest, ConnectCertificateResponse>());
  dispatcher.register_command("setDefaultCertificate",
                              CommandBuilder(certificates::handle_cert_set_default)
                                  .validate<SetDefaultCertificateRequest, SetDefaultCertificateResponse>());
  dispatcher.register_command("trustCertificate",
                              CommandBuilder(certificates::handle_cert_trust)
                                  .validate<TrustCertificateRequest, TrustCertificateResponse>());
  dispatcher.register_command("renameCertificate",
                              CommandBuilder(certificates::handle_cert_rename)
                                  .validate<RenameCertificateRequest, RenameCertificateResponse>());
}

void register_all_commands(CommandDispatcher &dispatcher)
{
  register_core_commands(dispatcher);
  register_certificates_commands(dispatcher);
  register_console_commands(dispatcher);
  register_ds_commands(dispatcher);
  register_job_commands(dispatcher);
  register_system_commands(dispatcher);
  register_uss_commands(dispatcher);
  register_tool_commands(dispatcher);
  register_tso_commands(dispatcher);
}

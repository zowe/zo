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

#include "tool.hpp"
#include "common_args.hpp"
#include "../zut.hpp"
#include "../zds.hpp"
#include "../zjb.hpp"
#include "../zuttype.h"
#include "ihapsa.h"
#include "ihaascb.h"
#include "ihaassb.h"
#include "iazjsab.h"
#include <unistd.h>
#include <map>
#include "../zdbg.h"

using namespace parser;
using namespace commands::common;

namespace tool
{

int handle_tool_convert_dsect(InvocationContext &context)
{
  int rc = 0;
  std::string resp;

  std::string adata_dsn = context.get<std::string>("adata-dsn", "");
  std::string chdr_dsn = context.get<std::string>("chdr-dsn", "");
  std::string sysprint = context.get<std::string>("sysprint", "");
  std::string sysout = context.get<std::string>("sysout", "");

  const char *user = getlogin();
  std::string struser(user);
  std::transform(struser.begin(), struser.end(), struser.begin(), ::tolower);

  if (sysprint.empty())
    sysprint = "/tmp/" + struser + "_sysprint.txt";
  if (sysout.empty())
    sysout = "/tmp/" + struser + "_sysout.txt";

  context.output_stream() << adata_dsn << " " << chdr_dsn << " " << sysprint << " " << sysout << std::endl;

  std::vector<std::string> dds;
  dds.reserve(4);
  dds.push_back("alloc fi(sysprint) path('" + sysprint + "') pathopts(owronly,ocreat,otrunc) pathmode(sirusr,siwusr,sirgrp) filedata(text) msg(2)");
  dds.push_back("alloc fi(sysout) path('" + sysout + "') pathopts(owronly,ocreat,otrunc) pathmode(sirusr,siwusr,sirgrp) filedata(text) msg(2)");
  dds.push_back("alloc fi(sysadata) da('" + adata_dsn + "') shr msg(2)");
  dds.push_back("alloc fi(edcdsect) da('" + chdr_dsn + "') shr msg(2)");

  ZDIAG diag{};
  rc = zut_loop_dynalloc(diag, dds);
  if (0 != rc)
  {
    context.error_stream() << "Error: allocation failed" << std::endl;
    context.error_stream() << "  Details: " << diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  rc = zut_convert_dsect();
  if (0 != rc)
  {
    context.error_stream() << "Error: convert failed with rc: '" << rc << "'" << std::endl;
    context.output_stream() << "  See '" << sysprint << "' and '" << sysout << "' for more details" << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }

  context.output_stream() << "DSECT converted to '" << chdr_dsn << "'" << std::endl;
  context.output_stream() << "Copy it via `cp \"//'" + chdr_dsn + "'\" <member>.h`" << std::endl;

  // Free dynalloc dds
  zut_free_dynalloc_dds(diag, dds);

  return rc;
}

int handle_tool_dynalloc(InvocationContext &context)
{
  int rc = 0;
  unsigned int code = 0;
  std::string resp;

  std::string parm = context.get<std::string>("parm", "");

  rc = zut_bpxwdyn(parm, &code, resp);
  if (0 != rc)
  {
    context.error_stream() << "Error: bpxwdyn with parm '" << parm << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << resp << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << resp << std::endl;

  return rc;
}

int handle_tool_search(InvocationContext &context)
{
  int rc = 0;

  std::string pattern = context.get<std::string>("string", "");
  std::string parms = context.get<std::string>("parms", "");
  std::string dsn = context.get<std::string>("dsn", "");
  std::string outdd_attributes = context.get<std::string>("outdd-attributes", "space(50,100)");

  // Perform dynalloc
  std::vector<std::string> dds;
  dds.reserve(3);
  dds.push_back("alloc dd(newdd) da('" + dsn + "') shr");
  dds.push_back("alloc dd(outdd) " + outdd_attributes);
  dds.push_back("alloc dd(sysin)");

  ZDIAG diag{};
  rc = zut_loop_dynalloc(diag, dds);
  if (0 != rc)
  {
    context.error_stream() << "Error: allocation failed" << std::endl;
    context.error_stream() << "  Details: " << diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  // Build super c selection criteria
  std::string data = " SRCHFOR '" + pattern + "'\n";

  // Write control statements
  ZDS zds{};
  ZDSWriteOpts write_opts{.zds = &zds, .ddname = "sysin"};
  zds_write(write_opts, data);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not write to dd: '"
                           << "sysin"
                           << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zds.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  std::transform(parms.begin(), parms.end(), parms.begin(), ::toupper);
  // Omit page breaks for programmatic callers like RPC that parse the listing
  if (context.is_redirecting_output() && parms.find("NOPRTCC") == std::string::npos)
  {
    parms += parms.empty() ? "NOPRTCC" : ",NOPRTCC";
  }

  // Perform search
  rc = zut_search(parms);
  if (RTNCD_SUCCESS != rc &&
      RTNCD_WARNING != rc &&
      ZUT_RTNCD_SEARCH_SUCCESS != rc &&
      ZUT_RTNCD_SEARCH_WARNING != rc)
  {
    context.error_stream() << "Error: could not invoke ISRSUPC rc: '" << rc << "'" << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }

  // Read output from super c
  std::string output;
  ZDS superc_zds{};
  ZDSReadOpts superc_read_opts{.zds = &superc_zds, .ddname = "OUTDD"};
  rc = zds_read(superc_read_opts, output);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not read from dd: '"
                           << "OUTDD"
                           << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << superc_zds.diag.e_msg << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }
  context.output_stream() << output << std::endl;

  // Free dynalloc dds
  zut_free_dynalloc_dds(diag, dds);

  return RTNCD_SUCCESS;
}

int handle_tool_amblist(InvocationContext &context)
{
  int rc = 0;

  std::string dsn = context.get<std::string>("dsn", "");
  std::string statements = " " + context.get<std::string>("control-statements", "");

  // Perform dynalloc
  std::vector<std::string> dds;
  dds.reserve(3);
  dds.push_back("alloc dd(syslib) da('" + dsn + "') shr");
  dds.push_back("alloc dd(sysprint) lrecl(80) recfm(f,b) blksize(80)");
  dds.push_back("alloc dd(sysin) lrecl(80) recfm(f,b) blksize(80)");

  ZDIAG diag{};
  rc = zut_loop_dynalloc(diag, dds);
  if (0 != rc)
  {
    context.error_stream() << "Error: allocation failed" << std::endl;
    context.error_stream() << "  Details: " << diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  std::transform(statements.begin(), statements.end(), statements.begin(), ::toupper);

  // Write control statements
  ZDS zds{};
  ZDSWriteOpts write_opts{.zds = &zds, .ddname = "sysin"};
  zds_write(write_opts, statements);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not write to dd: '"
                           << "sysin"
                           << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zds.diag.e_msg << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }

  // Perform search
  rc = zut_run("AMBLIST");
  if (RTNCD_SUCCESS != rc)
  {
    context.error_stream() << "Error: could not invoke AMBLIST rc: '" << rc << "'" << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }

  // Read output from amblist
  std::string output;
  ZDS amblist_zds{};
  ZDSReadOpts amblist_read_opts{.zds = &amblist_zds, .ddname = "SYSPRINT", .dsname = dsn};
  rc = zds_read(amblist_read_opts, output);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not read from dd: '"
                           << "SYSPRINT"
                           << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << amblist_zds.diag.e_msg << std::endl;
    zut_free_dynalloc_dds(diag, dds);
    return RTNCD_FAILURE;
  }
  context.output_stream() << output << std::endl;

  // Free dynalloc dds
  zut_free_dynalloc_dds(diag, dds);

  return RTNCD_SUCCESS;
}

std::string parse_escape_chars(std::string &input)
{
  std::string result;
  result.reserve(input.size());

  size_t i = 0;
  while (i < input.size())
  {
    if (input[i] == '\\' && i + 1 < input.size())
    {
      char next = input[i + 1];
      switch (next)
      {
      case 'n':
        result += '\n';
        i += 2;
        break;
      case 't':
        result += '\t';
        i += 2;
        break;
      case '\\':
        result += '\\';
        i += 2;
        break;
      case '"':
        result += '"';
        i += 2;
        break;
      default:
        // Unknown escape sequence, keep the backslash and the following character
        result += input[i];
        result += next;
        i += 2;
        break;
      }
    }
    else
    {
      result += input[i];
      ++i;
    }
  }

  return result;
}

int handle_tool_run(InvocationContext &context)
{
  int rc = 0;
  std::string program = context.get<std::string>("program", "");
  std::string parms = context.get<std::string>("parms", "");
  std::vector<std::string> dds;
  std::map<std::string, bool> dd_is_sysout;

  struct psa *psa = (struct psa *)0;
  struct ascb *ascb = (struct ascb *)psa->psaaold;
  struct assb *assb = (struct assb *)ascb->ascbassb;
  struct iazjsab *iezjsab = (struct iazjsab *)assb->assbjsab;

  const ArgumentMap &dynamic_args = context.dynamic_arguments();
  for (const auto &kv : dynamic_args)
  {
    std::string dd_spec = kv.second.get_string_value();
    dds.push_back("alloc dd(" + kv.first + ") " + dd_spec);

    // Check if this DD allocation contains "sysout"
    std::string dd_spec_lower = dd_spec;
    std::transform(dd_spec_lower.begin(), dd_spec_lower.end(), dd_spec_lower.begin(), ::tolower);
    dd_is_sysout[kv.first] = (dd_spec_lower.find("sysout") != std::string::npos);
  }

  ZDIAG diag{};
  rc = zut_loop_dynalloc(diag, dds);
  if (0 != rc)
  {
    context.error_stream() << "Error: allocation failed" << std::endl;
    context.error_stream() << "  Details: " << diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  std::string indd = context.get<std::string>("in-dd", "");
  if (indd.length() > 0)
  {
    std::string ddname = "DD:" + indd;
    std::ofstream out(ddname.c_str());
    if (!out.is_open())
    {
      context.error_stream() << "Error: could not open input '" << ddname << "'" << std::endl;
      if (dds.size() > 0)
      {
        context.error_stream() << "  Allocations: " << std::endl;
        for (const auto &dd : dds)
        {
          context.error_stream() << "    " << dd << std::endl;
        }
      }
      zut_free_dynalloc_dds(diag, dds);
      return RTNCD_FAILURE;
    }

    std::string input = context.get<std::string>("in-dd-parms", "");
    input = parse_escape_chars(input);

    if (context.has("in-dd-parms"))
    {
      out << input << std::endl;
    }

    out.close();
  }

  std::transform(program.begin(), program.end(), program.begin(), ::toupper);
  parms = parse_escape_chars(parms);

  rc = zut_run(diag, program, parms);

  if (0 != rc)
  {
    if (diag.e_msg_len > 0)
    {
      context.error_stream() << "Error: program '" << program << "' ended with rc: '" << rc << "'" << std::endl;
      context.error_stream() << "  Details: " << diag.e_msg << std::endl;
      if (dds.size() > 0)
      {
        context.error_stream() << "  Allocations: " << std::endl;
        for (const auto &dd : dds)
        {
          context.error_stream() << "    " << dd << std::endl;
        }
      }
    }
  }

  std::string outdd = context.get<std::string>("out-dd", "");
  if (outdd.length() > 0)
  {
    // Check if this DD was allocated as sysout
    bool is_sysout = dd_is_sysout.find(outdd) != dd_is_sysout.end() && dd_is_sysout[outdd];

    if (is_sysout)
    {
      ZJB zjb{};

      std::string jobid = std::string((char *)iezjsab->jsabjbid, sizeof(iezjsab->jsabjbid));

      // list all dsns on jobid
      std::vector<ZJobDD> job_dds;
      rc = zjb_list_dds(&zjb, jobid, job_dds);
      if (0 != rc)
      {
        return rc;
      }

      bool found = false;
      std::string dsname;
      memset(&zjb, 0, sizeof(ZJB));

      for (const auto &dd : job_dds)
      {
        std::string dd_ddn_trimmed = dd.ddn;
        std::string outdd_trimmed = outdd;
        std::transform(dd_ddn_trimmed.begin(), dd_ddn_trimmed.end(), dd_ddn_trimmed.begin(), ::toupper);
        std::transform(outdd_trimmed.begin(), outdd_trimmed.end(), outdd_trimmed.begin(), ::toupper);
        if (zut_trim(dd_ddn_trimmed) == zut_trim(outdd_trimmed))
        {
          found = true;
          dsname = dd.dsn;
          memcpy(zjb.token, dd.token, sizeof(dd.token));
          break;
        }
      }

      std::string output;
      rc = zjb_read_job_content_by_dsn(&zjb, dsname, output);
      if (0 != rc)
      {
        context.error_stream() << "Error: could not read from dd: '" << dsname << "' rc: '" << rc << "'" << std::endl;
        context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
        zut_free_dynalloc_dds(diag, dds);
        return RTNCD_FAILURE;
      }
      context.output_stream() << output;
    }
    else
    {
      // Regular DD handling (existing logic)
      std::string ddname = "DD:" + outdd;
      std::ifstream in(ddname.c_str());
      if (!in.is_open())
      {
        context.error_stream() << "Error: could not open output '" << ddname << "'" << std::endl;
        if (dds.size() > 0)
        {
          context.error_stream() << "  Allocations: " << std::endl;
          for (const auto &dd : dds)
          {
            context.error_stream() << "    " << dd << std::endl;
          }
        }
        zut_free_dynalloc_dds(diag, dds);
        return RTNCD_FAILURE;
      }

      std::string line;
      while (std::getline(in, line))
      {
        context.output_stream() << line << std::endl;
      }
      in.close();
    }
  }

  zut_free_dynalloc_dds(diag, dds);

  return rc;
}

void register_commands(parser::Command &root_command)
{
  auto tool_cmd = command_ptr(new Command("tool", "tool operations"));

  // Convert DSECT subcommand
  auto tool_convert_dsect_cmd = command_ptr(new Command("ccnedsct", "convert dsect to c struct"));
  tool_convert_dsect_cmd->add_keyword_arg("adata-dsn",
                                          make_aliases("--adata-dsn", "--ad"),
                                          "input adata dsn", ArgType_Single, true);
  tool_convert_dsect_cmd->add_keyword_arg("chdr-dsn",
                                          make_aliases("--chdr-dsn", "--cd"),
                                          "output chdr dsn", ArgType_Single, true);
  tool_convert_dsect_cmd->add_keyword_arg("sysprint",
                                          make_aliases("--sysprint", "--sp"),
                                          "sysprint output", ArgType_Single, false);
  tool_convert_dsect_cmd->add_keyword_arg("sysout",
                                          make_aliases("--sysout", "--so"),
                                          "sysout output", ArgType_Single, false);
  tool_convert_dsect_cmd->set_handler(handle_tool_convert_dsect);
  tool_cmd->add_command(tool_convert_dsect_cmd);

  // Dynalloc subcommand
  auto tool_dynalloc_cmd = command_ptr(new Command("bpxwdy2", "dynalloc command"));
  tool_dynalloc_cmd->add_positional_arg("parm", "dynalloc parm string", ArgType_Single, false);
  tool_dynalloc_cmd->set_handler(handle_tool_dynalloc);
  tool_cmd->add_command(tool_dynalloc_cmd);

  // Search subcommand
  auto tool_search_cmd = command_ptr(new Command("search", "search members for string with parms, e.g. --parms anyc"));
  tool_search_cmd->mark_stdout_as_payload();
  tool_search_cmd->add_positional_arg(DSN)
      .add_positional_arg("string", "string to search for", ArgType_Single, true)
      .add_keyword_arg("parms",
                       make_aliases("--parms", "--p"),
                       "parms to pass to ISRSUPC", ArgType_Single, false)
      .add_keyword_arg("outdd-attributes",
                       make_aliases("--outdd-attributes", "--oa"),
                       "attributes for output dd, e.g. 'space(50,100)'", ArgType_Single, false)
      .add_example("Search 'SYS1.MACLIB' for string 'world' with parms 'ANYC'",
                   "zo tool search sys1.maclib world --parms anyc")
      .add_example("Search 'SYS1.MACLIB' for string 'world' with parms 'ANYC,LPSF'",
                   "zo tool search sys1.maclib world --parms anyc,lpsf --outdd-attributes \"cylinders space(50,5)\"")
      .set_handler(handle_tool_search);
  tool_cmd->add_command(tool_search_cmd);

  // Amblist subcommand
  auto tool_amblist_cmd = command_ptr(new Command("amblist", "invoke amblist"));
  tool_amblist_cmd->add_positional_arg("dsn", "data containing input load modules", ArgType_Single, true);
  tool_amblist_cmd->add_keyword_arg("control-statements",
                                    make_aliases("--control-statements", "--cs"),
                                    "amblist control statements, e.g. listload output=map,member=testprog",
                                    ArgType_Single, true);
  tool_amblist_cmd->set_handler(handle_tool_amblist);
  tool_cmd->add_command(tool_amblist_cmd);

  // Run subcommand
  auto tool_run_cmd = command_ptr(new Command("run", "run a program"));
  tool_run_cmd->add_positional_arg("program", "name of program to run", ArgType_Single, true);
  tool_run_cmd->add_keyword_arg("in-dd",
                                make_aliases("--in-dd", "--idd"),
                                "input ddname", ArgType_Single, false);
  tool_run_cmd->add_keyword_arg("in-dd-parms",
                                make_aliases("--input", "--in"),
                                "input parameters written to in-dd", ArgType_Single, false);
  tool_run_cmd->add_keyword_arg("parms",
                                make_aliases("--parms", "--p"),
                                "parms to pass to program, PARMS=", ArgType_Single, false);
  tool_run_cmd->add_keyword_arg("out-dd",
                                make_aliases("--out-dd", "--odd"),
                                "output ddname", ArgType_Single, false);
  tool_run_cmd->enable_dynamic_keywords(ArgType_Single, "dd", "ddname(s) to allocate, e.g. --sysprint \"sysout\" --sysut1 \"da(MY.DATA.SET) shr msg(2)\"");
  tool_run_cmd->set_handler(handle_tool_run);
  tool_cmd->add_command(tool_run_cmd);

  root_command.add_command(tool_cmd);
}
} // namespace tool

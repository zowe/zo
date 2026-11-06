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

// O_NOFOLLOW is only defined under _XPLATFORM_SOURCE, which must precede the first system header.
#ifndef _XPLATFORM_SOURCE
#define _XPLATFORM_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#include "ztype.h"
#endif

#define _OPEN_SYS_EXT
#include <sys/ps.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include "zut.hpp"
#include "zlogger.hpp"
#include "zutm.h"
#include <ios>
#include "zuttype.h"
#include <vector>
#include <array>
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>
#include <stdlib.h>
#include <_Nascii.h>
#include "csvapfaa.h"
#include "iefjsqry.h"

void zut_strip_final_newline(std::string &input)
{
  if (!input.empty() && input.back() == '\n')
  {
    input.pop_back();
  }
}

static void zut_private_drain_fd(struct pollfd &pfd, std::string &output, pid_t pid);
static void zut_private_drain_pipes(std::array<struct pollfd, 2> &fds, std::string &stdout_response, std::string &stderr_response, pid_t pid);
static std::vector<const char *> zut_private_build_env(const std::string &command);
static void zut_extract_linklist_entries(DLAAHDR *answer, std::vector<ZLinklstEntry> &linklist);

int zut_private_run_program(const std::string &program, const std::vector<std::string> &args, std::string &stdout_response, std::string &stderr_response, bool merge_streams)
{
  stdout_response.clear();
  stderr_response.clear();

  if (0 == program.size())
  {
    if (merge_streams)
    {
      stdout_response = "Error: You must specify a program to run.";
    }
    else
    {
      stderr_response = "Error: You must specify a program to run.";
    }
    return RTNCD_FAILURE;
  }

  // pipefd[0] is for reading, pipefd[1] is for writing
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (-1 == pipe(stdout_pipe))
  {
    return RTNCD_FAILURE;
  }

  if (-1 == pipe(stderr_pipe))
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return RTNCD_FAILURE;
  }

  int devnull_fd = open("/dev/null", O_RDONLY); // for use with spawn'd stdin
  if (devnull_fd == -1)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return RTNCD_FAILURE;
  }

  std::vector<char *> argv_vec;
  argv_vec.reserve(args.size() + 2); // 2 = program + ending nullptr
  argv_vec.push_back(const_cast<char *>(program.c_str()));
  for (const auto &arg : args)
  {
    argv_vec.push_back(const_cast<char *>(arg.c_str()));
  }
  argv_vec.push_back(nullptr);

  int fd_count = 3;
  int fd_map[3];

  fd_map[0] = devnull_fd; // set to /dev/null - no unintentional writes to stdin

  // Child fd 1 (stdout): Map to the write end of the stdout pipe
  fd_map[1] = stdout_pipe[1];

  // Child fd 2 (stderr): Map to stderr pipe, or merge with stdout pipe
  if (merge_streams)
  {
    fd_map[2] = stdout_pipe[1];
  }
  else
  {
    fd_map[2] = stderr_pipe[1];
  }

  std::vector<const char *> env_vec = zut_private_build_env(program);
  struct inheritance inherit = {};

  pid_t pid = spawnp(program.c_str(), fd_count, fd_map, &inherit, (const char **)argv_vec.data(), env_vec.data());

  close(devnull_fd); // close /dev/null fd right away; child has a clone

  if (pid == -1)
  {
    int spawn_error = errno;
    std::string error_message;
    if (spawn_error == ENOENT)
    {
      error_message = "zut_private_run_program: " + program + ": command not found";
    }
    else if (spawn_error == EACCES)
    {
      error_message = "zut_private_run_program: Permission denied when trying to execute '" + program + "'.";
    }
    else
    {
      error_message = "zut_private_run_program: error running " + program + ": " + std::string(strerror(spawn_error));
    }
    if (merge_streams)
    {
      stdout_response = error_message;
    }
    else
    {
      stderr_response = error_message;
    }
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return RTNCD_FAILURE;
  }

  // The parent doesn't need the write ends of the pipes
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  std::array<struct pollfd, 2> fds = {{{stdout_pipe[0], POLLIN, 0},
                                       {merge_streams ? -1 : stderr_pipe[0], POLLIN, 0}}};

  zut_private_drain_pipes(fds, stdout_response, stderr_response, pid);

  zut_strip_final_newline(stdout_response);
  zut_strip_final_newline(stderr_response);

  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  // wait for the child process to finish and get its exit status
  int status;
  if (-1 == waitpid(pid, &status, 0))
  {
    return RTNCD_FAILURE;
  }

  // Evaluate the exit status
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }

  return RTNCD_FAILURE;
}

int zut_run_program(const std::string &program, const std::vector<std::string> &args, std::string &stdout_response, std::string &stderr_response)
{
  return zut_private_run_program(program, args, stdout_response, stderr_response, false);
}

int zut_run_program(const std::string &program, const std::vector<std::string> &args, std::string &response)
{
  std::string dummy;
  return zut_private_run_program(program, args, response, dummy, true);
}

static void zut_private_drain_fd(struct pollfd &pfd, std::string &output, pid_t pid)
{
  if (pfd.fd == -1)
  {
    return;
  }

  if (pfd.revents & POLLIN)
  {
    std::array<char, 4096> buf;
    ssize_t n = read(pfd.fd, buf.data(), buf.size());
    if (n > 0)
    {
      output.append(buf.data(), n);
    }
    else if (0 == n)
    {
      pfd.fd = -1;
    }
    else if (-1 == n && EINTR != errno)
    {
      kill(pid, SIGKILL);
      pfd.fd = -1;
    }
    return;
  }

  if (pfd.revents & (POLLHUP | POLLERR))
  {
    pfd.fd = -1;
  }
}

static void zut_private_drain_pipes(std::array<struct pollfd, 2> &fds,
                                    std::string &stdout_response,
                                    std::string &stderr_response,
                                    pid_t pid)
{
  while (fds[0].fd != -1 || fds[1].fd != -1)
  {
    if (-1 == poll(fds.data(), fds.size(), -1))
    {
      if (EINTR != errno)
      {
        kill(pid, SIGKILL);
        break;
      }
      continue;
    }

    zut_private_drain_fd(fds[0], stdout_response, pid);
    zut_private_drain_fd(fds[1], stderr_response, pid);
  }
}

/**
 * Commands that change process identity (UID/GID) and are not supported
 * when _BPX_SHAREAS=YES (shared address space). See e.g. IBM doc for newgrp.
 * When the user runs one of these, we must not pass _BPX_SHAREAS=YES to the child.
 */
static constexpr std::array<const char *, 4> ZUT_NOSHAREAS_COMMANDS = {"newgrp", "su", "sg", "zoa"};

static bool zut_private_command_requires_noshareas(const std::string &command)
{
  std::string s = command;
  const std::string whitespace(" \t\n");
  size_t pos = s.find_first_not_of(whitespace);
  if (pos == std::string::npos)
  {
    return false;
  }
  while (pos < s.size())
  {
    size_t end = pos;
    while (end < s.size() && s[end] != ' ' && s[end] != '\t' && s[end] != '\n')
    {
      ++end;
    }
    std::string token = s.substr(pos, end - pos);
    pos = (end < s.size()) ? s.find_first_not_of(whitespace, end) : s.size();

    if (token.empty())
    {
      continue;
    }
    size_t eq = token.find('=');
    if (eq != std::string::npos && eq > 0)
    {
      continue; // env assignment (e.g. FOO=bar), skip
    }
    size_t last_slash = token.rfind('/');
    std::string basename = (last_slash != std::string::npos) ? token.substr(last_slash + 1) : token;
    for (const char *cmd : ZUT_NOSHAREAS_COMMANDS)
    {
      if (basename == cmd)
      {
        return true;
      }
    }
    return false;
  }
  return false;
}

static std::string zut_private_get_shell()
{
  std::string shell_path = "/bin/sh";

  // Check if /bin/sh exists AND is executable by the current user
  if (access(shell_path.c_str(), X_OK) != 0)
  {
    // otherwise, try env. If it's empty, we leave shell_path=/bin/sh
    const char *env_shell = std::getenv("SHELL");
    if (env_shell != nullptr && env_shell[0] != '\0')
    {
      shell_path = env_shell;
    }
  }

  return shell_path;
}

static std::vector<const char *> zut_private_build_env(const std::string &command)
{
  extern char **environ; // NOSONAR: POSIX-mandated global, cannot be const
  std::vector<const char *> env_vec;
  bool has_bpx_shareas = false;
  const bool use_noshareas = zut_private_command_requires_noshareas(command);

  for (char **ep = environ; ep != nullptr && *ep != nullptr; ++ep)
  {
    if (0 == strncmp(*ep, "_BPX_SHAREAS=", 13))
    {
      has_bpx_shareas = true;
      if (use_noshareas)
      {
        continue; // do not pass _BPX_SHAREAS to child for identity-changing commands
      }
    }
    env_vec.push_back(*ep);
  }
  if (!use_noshareas && !has_bpx_shareas)
  {
    env_vec.push_back("_BPX_SHAREAS=YES");
  }
  env_vec.push_back(nullptr);
  return env_vec;
}

int zut_spawn_shell_command(const std::string &command, std::string &stdout_response, std::string &stderr_response)
{
  stdout_response.clear();
  stderr_response.clear();

  if (0 == command.size())
  {
    stderr_response = "Error: You must specify a program to run.";
    return RTNCD_FAILURE;
  }

  const std::string shell_program = zut_private_get_shell();
  std::vector<std::string> argv_vec = {"-c", command};
  return zut_private_run_program(shell_program, argv_vec, stdout_response, stderr_response, false);
}

int zut_search(const std::string &parms)
{
  return ZUTSRCH(parms.c_str());
}

int zut_run(ZDIAG &diag, const std::string &program, const std::string &parms)
{
  return ZUTRUN(&diag, program.c_str(), parms.c_str());
}

int zut_run(const std::string &program)
{
  ZDIAG diag{};
  return ZUTRUN(&diag, program.c_str(), nullptr);
}

/**
 * @brief Build DFSMSdfp DD options for use with zutm utilities.
 * @param dd_list Reference to DFSMSdfp DD list built by this function.
 * @param alt_dds Reference to DFSMSdfp ALT DD options.
 * @return void
 */
static void zut_build_dfsmsdfp_dds_options(DFSMSdfp_DD_LIST &dd_list, const DFSMSdfp_AltDDs &alt_dds)
{
  // represents length of the DD list in IEBCOPY_ALT_DDS
  // see https://www.ibm.com/docs/en/SSLTBW_3.1.0/pdf/idau100_v3r1.pdf, "ddname List"
  constexpr uint16_t DD_LEN_SYSUT2 = 72;
  constexpr uint16_t DD_LEN_SYSUT3 = 80;
  constexpr uint16_t DD_LEN_SYSUT4 = 88;

  uint16_t len = DD_LEN_SYSUT2;
  zut_set_dd_with_padding(dd_list.sysin, alt_dds.sysin.c_str());
  zut_set_dd_with_padding(dd_list.sysprint, alt_dds.sysprint.c_str());
  zut_set_dd_with_padding(dd_list.sysut1, alt_dds.sysut1.c_str());
  zut_set_dd_with_padding(dd_list.sysut2, alt_dds.sysut2.c_str());
  if (alt_dds.sysut3.has_value())
  {
    zut_set_dd_with_padding(dd_list.sysut3, alt_dds.sysut3.value().c_str());
    len = DD_LEN_SYSUT3;
  }
  if (alt_dds.sysut4.has_value())
  {
    zut_set_dd_with_padding(dd_list.sysut4, alt_dds.sysut4.value().c_str());
    len = DD_LEN_SYSUT4;
  }
  dd_list.TotalLength = len;
}

/**
 * @brief Run zutm DFSMSdfp utility with the given options and DD list.
 * @param diag Reference to diagnostic information structure.
 * @param utility Which supported DFSMSdfp utility to run.
 * @param opts Options string for the DFSMSdfp utility.
 * @param alt_dds Alternate DD options for the DFSMSdfp utility.
 * @return The return code from the service.
 */
static int zut_dfsmsdfp(ZDIAG &diag, ZUTMSDFP_UTILITY utility, const std::string &opts, const DFSMSdfp_AltDDs *alt_dds)
{
  DFSMSdfp_OPT_LIST opt_list = {0};
  DFSMSdfp_DD_LIST dd_list = {0};

  if (!opts.empty())
  {
    opt_list.len = snprintf(opt_list.str, sizeof(opt_list.str) - 1, "%s", opts.c_str());
  }

  if (alt_dds)
  {
    zut_build_dfsmsdfp_dds_options(dd_list, *alt_dds);
  }

  return ZUTMSDFP(&diag, &utility, &opt_list, &dd_list);
}

int zut_iebcopy(ZDIAG &diag, const std::string &opts, const DFSMSdfp_AltDDs *alt_dd_list)
{
  return zut_dfsmsdfp(diag, ZUTMSDFP_IEBCOPY, opts, alt_dd_list);
}

int zut_iebgener(ZDIAG &diag, const std::string &opts, const DFSMSdfp_AltDDs *alt_dd_list)
{
  return zut_dfsmsdfp(diag, ZUTMSDFP_IEBGENER, opts, alt_dd_list);
}

void zut_set_dd_with_padding(char arr[8], const char *ddname)
{
  if (ddname && ddname[0] != '\0')
  {
    memset(arr, ' ', 8); // Pad with spaces for standard DD format
    size_t len = strlen(ddname);
    memcpy(arr, ddname, len > 8 ? 8 : len);
  }
  else
  {
    memset(arr, 0, 8); // Binary zeros
  }
}

unsigned char zut_get_key()
{
  return ZUTMGKEY();
}

int zut_substitute_symbol(const std::string &pattern, std::string &result)
{
  SYMBOL_DATA *parms = (SYMBOL_DATA *)__malloc31(sizeof(SYMBOL_DATA));
  if (parms == nullptr)
  {
    return RTNCD_FAILURE;
  }
  memset(parms, 0x00, sizeof(SYMBOL_DATA));

  if (pattern.size() >= sizeof(parms->input))
  {
    free(parms);
    return RTNCD_FAILURE;
  }

  strncpy(parms->input, pattern.c_str(), sizeof(parms->input) - 1);
  parms->input[sizeof(parms->input) - 1] = '\0';
  parms->length = std::strlen(pattern.c_str());
  int rc = ZUTSYMBP(parms);
  if (RTNCD_SUCCESS != rc)
  {
    free(parms);
    return rc;
  }
  parms->output[sizeof(parms->output) - 1] = '\0';
  result += std::string(parms->output);
  free(parms);
  return RTNCD_SUCCESS;
}

int zut_convert_dsect()
{
  return ZUTEDSCT();
}

void zut_uppercase_pad_truncate(char *target, std::string source, int len)
{
  memset(target, ' ', len);                                                // pad with spaces
  std::transform(source.begin(), source.end(), source.begin(), ::toupper); // upper case
  int length = source.size() > len ? len : source.size();                  // truncate
  strncpy(target, source.c_str(), length);
}

// https://www.ibm.com/docs/en/zos/3.2.0?topic=output-requesting-dynamic-allocation
int zut_bpxwdyn_common(const std::string &parm, unsigned int *code, std::string &resp, std::string &ddname, std::string &dsname)
{
  char bpx_response[RET_ARG_MAX_LEN * MSG_ENTRIES + 1] = {0};

  unsigned char *p = (unsigned char *)__malloc31(sizeof(BPXWDYN_PARM) + sizeof(BPXWDYN_RESPONSE));
  if (p == nullptr)
  {
    return RTNCD_FAILURE;
  }
  memset(p, 0x00, sizeof(BPXWDYN_PARM) + sizeof(BPXWDYN_RESPONSE));

  BPXWDYN_PARM *bparm = (BPXWDYN_PARM *)p;
  if (parm.size() >= sizeof(bparm->str))
  {
    return RTNCD_FAILURE;
  }

  BPXWDYN_RESPONSE *response = (BPXWDYN_RESPONSE *)(p + sizeof(BPXWDYN_PARM));

  if (ddname == "RTDDN")
  {
    bparm->rtdd = 1; // set bit flag indicating we want to return the DD name
  }
  else if (dsname == "RTDSN")
  {
    bparm->rtdsn = 1; // set bit flag indicating we want to return the DS name
  }

  bparm->len = snprintf(bparm->str, sizeof(bparm->str), "%s", parm.c_str());

  int rc = ZUTWDYN(bparm, response);

  if (bparm->rtdd)
  {
    ddname = std::string(response->ddname);
  }
  else if (bparm->rtdsn)
  {
    dsname = std::string(response->dsname);
  }

  resp = std::string(response->response);
  *code = response->code;

  free(p);

  return rc;
}

int zut_bpxwdyn(const std::string &parm, unsigned int *code, std::string &resp)
{
  std::string ddname = "";
  std::string dsname = "";
  return zut_bpxwdyn_common(parm, code, resp, ddname, dsname);
}

int zut_bpxwdyn_rtdd(const std::string &parm, unsigned int *code, std::string &resp, std::string &ddname)
{
  ddname = "RTDDN";
  std::string dsname = "";
  return zut_bpxwdyn_common(parm, code, resp, ddname, dsname);
}

int zut_bpxwdyn_rtdsn(const std::string &parm, unsigned int *code, std::string &resp, std::string &dsname)
{
  std::string ddname = "";
  dsname = "RTDSN";
  return zut_bpxwdyn_common(parm, code, resp, ddname, dsname);
}

std::string zut_build_etag(const size_t mtime, const size_t byte_size)
{
  std::stringstream ss;
  ss << std::hex << mtime;
  ss << "-";
  ss << std::hex << byte_size;
  return ss.str();
}

int zut_get_current_user(std::string &struser)
{
  int rc = 0;
  char user[9] = {0};

  rc = __getuserid(user, sizeof(user));
  if (0 != rc)
    return rc;

  struser = std::string(user);
  zut_rtrim(struser);
  return rc;
}

int zut_hello(const std::string &name)
{
  // #if defined(__IBMC__) || defined(__IBMCPP__)
  // #pragma convert(819)
  // #endif

  if (name.empty())
    std::cout << "Hello world!" << std::endl;
  else
    std::cout << "Hello " << name << std::endl;
  return 0;

  // #if defined(__IBMC__) || defined(__IBMCPP__)
  // #pragma convert(0)
  // #endif

  return 0;
}

int zut_list_subsystems(ZDIAG &diag, std::vector<std::string> &subsystems, std::string filter)
{
  int rc = 0;
  JQRY_HEADER *area = nullptr;

  rc = ZUTSSIQ(&diag, &area, filter.c_str());

  if (0 != rc)
  {
    return rc;
  }

  JQRY_SUBSYS_ENTRY *subsys_entry = nullptr;
  subsys_entry = (JQRY_SUBSYS_ENTRY *)((unsigned char *)area + sizeof(JQRY_HEADER));
  int next_entry = sizeof(JQRY_HEADER) + sizeof(JQRY_VT_ENTRY) * 2; // NOTE(Kelosky): 2 is the number of vector tables

  for (int i = 0; i < area->jqry___num___subsys; i++)
  {
    const unsigned char *name = subsys_entry->jqry___subsys___name;
    bool printable = true;
    for (size_t j = 0; j < sizeof(subsys_entry->jqry___subsys___name); j++)
    {
      if (!isprint(name[j]))
      {
        printable = false;
        break;
      }
    }
    if (printable)
    {
      subsystems.push_back(std::string(reinterpret_cast<const char *>(name), sizeof(subsys_entry->jqry___subsys___name)));
    }
    else
    {
      char hex[13];
      snprintf(hex, sizeof(hex), "x'%02X%02X%02X%02X'", name[0], name[1], name[2], name[3]);
      subsystems.push_back(std::string(hex));
    }
    subsys_entry = (JQRY_SUBSYS_ENTRY *)((unsigned char *)subsys_entry + next_entry);
  }

  ZUTMSREL(&area->jqrylen, area);

  return rc;
}

int zut_list_apf(ZDIAG &diag, std::vector<std::pair<std::string, std::string>> &apf)
{
  int rc = 0;
  int size = sizeof(struct apfhdr);
  struct apfhdr *answer_fixed = (struct apfhdr *)__malloc31(size);
  int answer_len = size;
  int rsn = 0;
  rc = ZUTMAPFQ(&diag, answer_fixed, &answer_len, &rsn);
  if (0 != rc)
  {
    if (csvapfrsnnotalldatareturned == rsn)
    {
      int needed_len = answer_fixed->apfhtlen;
      struct apfhdr *answer_dynamic = (struct apfhdr *)__malloc31(needed_len);
      rc = ZUTMAPFQ(&diag, answer_dynamic, &needed_len, &rsn);

      if (0 != rc)
      {
        free(answer_fixed);
        free(answer_dynamic);
        return rc;
      }

      struct apfe *apfe = (struct apfe *)((unsigned char *)answer_dynamic + answer_dynamic->apfhoff);
      for (int i = 0; i < answer_dynamic->apfh_n_rec; i++)
      {
        std::string dsn = std::string((char *)apfe->apfedsname, apfe->apfedslen);
        std::string volume = std::string((char *)apfe->apfevolume, sizeof(apfe->apfevolume));
        apf.push_back(std::make_pair(dsn, volume));
        apfe = (struct apfe *)((unsigned char *)apfe + apfe->apfelen);
      }
      free(answer_dynamic);
    }
    else
    {
      free(answer_fixed);
      return rc;
    }
  }

  free(answer_fixed);
  return rc;
}

static void zut_extract_linklist_entries(DLAAHDR *answer, std::vector<ZLinklstEntry> &linklist)
{
  DLAALS *ls = (DLAALS *)answer->dlaahfirstlsaddr;
  for (int i = 0; i < answer->dlaahnumls; i++)
  {
    DLAADS *ds = (DLAADS *)ls->dlaalsfirstdsaddr;
    for (int j = 0; j < ls->dlaalsnumds; j++)
    {
      ZLinklstEntry entry;
      entry.dsname = std::string((char *)ds->dlaadsname, ds->dlaadsnamelen);
      entry.volume = std::string((char *)ds->dlaadsvolid, sizeof(ds->dlaadsvolid));
      entry.apf = (ds->dlaadsflags & dlaadsapf) != 0;
      linklist.push_back(entry);
      ds = (DLAADS *)ds->dlaadsnextaddr;
    }
    ls = (DLAALS *)ls->dlaalsnextaddr;
  }
}

int zut_list_linklist(ZDIAG &diag, std::vector<ZLinklstEntry> &linklist)
{
  int rc = 0;
  int size = sizeof(DLAAHDR);
  DLAAHDR *answer_fixed = (DLAAHDR *)__malloc31(size);
  int answer_len = size;
  int rsn = 0;
  rc = ZUTMDYNQ(&diag, answer_fixed, &answer_len, &rsn);
  if (0 != rc)
  {
    if (csvdynlrsnnotalldatareturned == rsn)
    {
      int needed_len = answer_fixed->dlaahtlen;
      DLAAHDR *answer_dynamic = (DLAAHDR *)__malloc31(needed_len);
      rc = ZUTMDYNQ(&diag, answer_dynamic, &needed_len, &rsn);

      if (0 != rc)
      {
        free(answer_fixed);
        free(answer_dynamic);
        return rc;
      }

      zut_extract_linklist_entries(answer_dynamic, linklist);
      free(answer_dynamic);
    }
    else
    {
      free(answer_fixed);
      return rc;
    }
  }
  else
  {
    zut_extract_linklist_entries(answer_fixed, linklist);
  }

  free(answer_fixed);
  return rc;
}

int zut_list_parmlib(ZDIAG &diag, std::vector<std::string> &parmlibs)
{
  int rc = 0;
  PARMLIB_DSNS dsns = {0};
  int num_dsns = 0;

  ZLOG_DEBUG("zut_list_parmlib: calling ZUTMLPLB");
  rc = ZUTMLPLB(&diag, &num_dsns, &dsns);
  ZLOG_DEBUG("zut_list_parmlib: ZUTMLPLB returned rc=%d num_dsns=%d (positive=%d)", rc, num_dsns, num_dsns > 0 ? 1 : 0);
  if (0 != rc)
  {
    return rc;
  }

  if (num_dsns < 0 || num_dsns > MAX_PARMLIB_DSNS)
  {
    ZLOG_WARN("zut_list_parmlib: clamping num_dsns from %d to [0,%d]", num_dsns, MAX_PARMLIB_DSNS);
    num_dsns = num_dsns < 0 ? 0 : MAX_PARMLIB_DSNS;
  }

  parmlibs.reserve(static_cast<size_t>(num_dsns));
  for (int i = 0; i < num_dsns; i++)
  {
    std::string dsn(dsns.dsn[i].val, sizeof(dsns.dsn[i].val));
    parmlibs.emplace_back(zut_rtrim(dsn));
  }

  return rc;
}

char zut_get_hex_char(int num)
{
  return "0123456789ABCDEF"[num & 0xF];
}

// built from pseudocode in https://en.wikipedia.org/wiki/Adler-32#Calculation
void zut_adler32_update(Adler32State &state, const char *data, size_t len)
{
  const uint32_t MOD_ADLER = 65521u;
  const size_t block_size = 16;
  size_t i = 0;

  // Process data in blocks of 16 bytes
  while (i + block_size <= len)
  {
    for (size_t j = 0; j < block_size; j++)
    {
      state.a += static_cast<uint8_t>(data[i + j]);
      state.b += state.a;
    }

    state.a %= MOD_ADLER;
    state.b %= MOD_ADLER;
    i += block_size;
  }

  // Process remaining bytes
  while (i < len)
  {
    state.a += static_cast<uint8_t>(data[i]);
    state.b += state.a;
    i++;
  }

  state.a %= MOD_ADLER;
  state.b %= MOD_ADLER;
}

uint32_t zut_adler32_finalize(const Adler32State &state)
{
  return (state.b << 16) | state.a;
}

uint32_t zut_calc_adler32_checksum(const std::string &input)
{
  Adler32State state;
  zut_adler32_update(state, input.data(), input.length());
  return zut_adler32_finalize(state);
}

/**
 * Prints the input string as bytes to the specified output stream.
 * @param input The input string to be printed.
 * @param output_stream Pointer to output stream (nullptr uses std::cout).
 */
void zut_print_string_as_bytes(std::string &input, std::ostream *out_stream)
{
  std::ostream &output_stream = out_stream ? *out_stream : std::cout;
  char buf[4];
  for (char *p = (char *)input.data(); p < (input.data() + input.length()); p++)
  {
    if (p == (input.data() + input.length() - 1))
    {
      snprintf(buf, sizeof(buf), "%02x", (unsigned char)*p);
    }
    else
    {
      snprintf(buf, sizeof(buf), "%02x ", (unsigned char)*p);
    }
    output_stream << buf;
  }
  output_stream << std::endl;
}

/**
 * Converts a zero-padded hex string to a vector of bytes (e.g. "010203" -> [0x01, 0x02, 0x03]).
 * @param hex_string The hex string to convert.
 * @return A vector of bytes representing the hex string.
 */
std::vector<uint8_t> zut_get_contents_as_bytes(const std::string &hex_string)
{
  std::vector<uint8_t> bytes;
  bytes.reserve(hex_string.length() / 2);

  // If the hex string is not zero-padded, return an empty vector
  if (hex_string.length() % 2 != 0)
  {
    return bytes;
  }

  for (auto i = 0u; i < hex_string.size(); i += 2u)
  {
    const auto byte_str = hex_string.substr(i, 2);
    bytes.push_back(strtoul(byte_str.c_str(), nullptr, 16));
  }

  return bytes;
}

/**
 * Prepares the encoding options.
 *
 * @param encoding_value - The value of the encoding option.
 * @param opts - Pointer to the ZEncode options.
 *
 * @return true if the encoding options are successfully prepared, false otherwise.
 */
bool zut_prepare_encoding(const std::string &encoding_value, ZEncode *opts)
{
  if (encoding_value.empty() || opts == nullptr)
  {
    return false;
  }

  if (encoding_value.size() < sizeof(opts->codepage))
  {
    memcpy(opts->codepage, encoding_value.data(), encoding_value.length() + 1);
    opts->data_type = encoding_value == "binary" ? eDataTypeBinary : eDataTypeText;
    return true;
  }

  return false;
}

/**
 * Converts a string from one encoding to another using the `iconv` function.
 *
 * @param cd `iconv` conversion descriptor
 * @param data required data (input, input size, output pointers) for conversion
 * @param diag diagnostic structure to store error information
 * @param flush_state If true, flush the shift state for stateful encodings (e.g., IBM-939). Set to true on the last chunk.
 *
 * @return return code from `iconv`
 */
size_t zut_iconv(iconv_t cd, ZConvData &data, ZDIAG &diag, bool flush_state)
{
  size_t input_bytes_remaining = data.input_size;
  size_t output_bytes_remaining = data.max_output_size;

  size_t rc = iconv(cd, &data.input, &input_bytes_remaining, &data.output_iter, &output_bytes_remaining);

  // If an error occurred, throw an exception with iconv's return code and errno
  if (-1 == rc)
  {
    ZDIAG_SET_MSG(&diag, "[zut_iconv] Error when converting characters. rc=%zu,errno=%d", rc, errno);
    return -1;
  }

  // "If the input conversion is stopped... the value pointed to by inbytesleft will be nonzero and errno is set to indicate the condition"
  if (0 != input_bytes_remaining)
  {
    ZDIAG_SET_MSG(&diag, "[zut_iconv] Failed to convert all input bytes. rc=%zu,errno=%d", rc, errno);
    return -1;
  }

  // Flush the shift state for stateful encodings (e.g., IBM-939 with SI/SO sequences)
  if (flush_state)
  {
    size_t flush_rc = iconv(cd, nullptr, nullptr, &data.output_iter, &output_bytes_remaining);
    if (-1 == flush_rc)
    {
      ZDIAG_SET_MSG(&diag, "[zut_iconv] Error flushing shift state. rc=%zu,errno=%d", flush_rc, errno);
      return -1;
    }
  }

  return rc;
}

/**
 * Flushes the shift state for stateful encodings (e.g., IBM-939 with SI/SO sequences).
 * This should be called after all chunks have been processed.
 *
 * @param cd `iconv` conversion descriptor
 * @param diag diagnostic structure to store error information
 *
 * @return vector containing the flushed bytes (empty on error)
 */
std::vector<char> zut_iconv_flush(iconv_t cd, ZDIAG &diag)
{
  const size_t max_output_size = 16; // Small buffer for shift state flush (SI/SO sequences are typically 1-2 bytes)
  std::vector<char> output_buffer(max_output_size, 0);
  char *output_iter = &output_buffer[0];
  size_t output_bytes_remaining = max_output_size;

  char *start_pos = output_iter;
  size_t flush_rc = iconv(cd, nullptr, nullptr, &output_iter, &output_bytes_remaining);
  if (-1 == flush_rc)
  {
    ZDIAG_SET_MSG(&diag, "[zut_iconv_flush] Error flushing shift state. rc=%zu,errno=%d", flush_rc, errno);
    return std::vector<char>(); // Return empty vector on error
  }

  // Resize to actual bytes written
  size_t flush_bytes = output_iter - start_pos;
  output_buffer.resize(flush_bytes);
  return output_buffer;
}

/**
 * Converts the encoding for a string from one codepage to another.
 * @param input_str input data to convert
 * @param from_encoding current codepage for the input data
 * @param to_encoding desired codepage for the data
 * @param diag diagnostic structure to store error information
 */
std::string zut_encode(const std::string &input_str, const std::string &from_encoding, const std::string &to_encoding, ZDIAG &diag)
{
  if (from_encoding == to_encoding)
  {
    return input_str;
  }

  std::vector<char> result = zut_encode(input_str.data(), input_str.size(), from_encoding, to_encoding, diag);
  return std::string(result.begin(), result.end());
}

/**
 * Converts the encoding for a string from one codepage to another.
 * @param input_str input data to convert
 * @param input_size size of the input data in bytes
 * @param from_encoding current codepage for the input data
 * @param to_encoding desired codepage for the data
 * @param diag diagnostic structure to store error information
 */
std::vector<char> zut_encode(const char *input_str, const size_t input_size, const std::string &from_encoding, const std::string &to_encoding, ZDIAG &diag)
{
  if (from_encoding == to_encoding)
  {
    return std::vector<char>(input_str, input_str + input_size);
  }

  iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
  if (cd == (iconv_t)(-1))
  {
    ZDIAG_SET_MSG(&diag, "Cannot open converter from %s to %s", from_encoding.c_str(), to_encoding.c_str());
    return std::vector<char>();
  }

  // maximum possible size assumes UTF-8 data with 4-byte character sequences
  const size_t max_output_size = input_size * 4;

  std::vector<char> output_buffer(max_output_size, 0);

  // Prepare iconv parameters (copy output_buffer ptr to output_iter to cache start and end positions)
  char *input = const_cast<char *>(input_str);
  char *output_iter = &output_buffer[0];

  ZConvData data = {input, input_size, max_output_size, &output_buffer[0], output_iter};
  size_t iconv_rc = zut_iconv(cd, data, diag);
  iconv_close(cd);
  if (-1 == iconv_rc)
  {
    throw std::runtime_error(diag.e_msg);
  }

  // Shrink output buffer and return it to the caller
  output_buffer.resize(data.output_iter - data.output_buffer);
  return output_buffer;
}

/**
 * Converts the encoding for a string using an existing iconv descriptor.
 * @param input_str input data to convert
 * @param cd iconv descriptor (caller manages opening, flushing, and closing)
 * @param diag diagnostic structure to store error information
 */
std::string zut_encode(const std::string &input_str, iconv_t cd, ZDIAG &diag, bool flush_state)
{
  std::vector<char> result = zut_encode(input_str.data(), input_str.size(), cd, diag, flush_state);
  return std::string(result.begin(), result.end());
}

/**
 * Converts the encoding for a string using an existing iconv descriptor.
 * @param input_str input data to convert
 * @param input_size size of the input data in bytes
 * @param cd iconv descriptor (caller manages opening, flushing, and closing)
 * @param diag diagnostic structure to store error information
 * @param flush_state when true, flush the shift state after the input so the
 *   returned bytes are a self-contained unit (ends back in single-byte state)
 */
std::vector<char> zut_encode(const char *input_str, const size_t input_size, iconv_t cd, ZDIAG &diag, bool flush_state)
{
  // Headroom beyond the *4 worst case so the trailing shift sequence (e.g. SI)
  // produced when flush_state is set always fits, even for an empty input.
  const size_t max_output_size = input_size * 4 + 16;
  std::vector<char> output_buffer(max_output_size, 0);

  char *input = const_cast<char *>(input_str);
  char *output_iter = &output_buffer[0];

  ZConvData data = {input, input_size, max_output_size, &output_buffer[0], output_iter};

  size_t iconv_rc = zut_iconv(cd, data, diag, flush_state);
  if (-1 == iconv_rc)
  {
    throw std::runtime_error(diag.e_msg);
  }

  output_buffer.resize(data.output_iter - data.output_buffer);
  return output_buffer;
}

std::string &zut_rtrim(std::string &s, const char *t)
{
  return s.erase(s.find_last_not_of(t) + 1);
}

std::string &zut_ltrim(std::string &s, const char *t)
{
  return s.erase(0, s.find_first_not_of(t));
}

std::string &zut_trim(std::string &s, const char *t)
{
  return zut_ltrim(zut_rtrim(s, t), t);
}

std::string zut_format_as_csv(std::vector<std::string> &fields)
{
  std::string formatted;
  for (int i = 0; i < fields.size(); i++)
  {
    formatted += zut_trim(fields.at(i));
    if (i < fields.size() - 1)
    {
      formatted += ",";
    }
  }

  return formatted;
}

bool zut_string_compare_c(const std::string &a, const std::string &b)
{
  return strcmp(a.c_str(), b.c_str()) < 0;
}

int zut_loop_dynalloc(ZDIAG &diag, const std::vector<std::string> &list)
{
  int rc = 0;
  unsigned int code = 0;
  std::string response;

  for (const auto &alloc : list)
  {
    rc = zut_bpxwdyn(alloc, &code, response);

    if (0 != rc)
    {
      diag.detail_rc = ZUT_RTNCD_SERVICE_FAILURE;
      diag.service_rc = rc;
      strcpy(diag.service_name, "bpxwdyn");
      ZDIAG_SET_MSG(&diag, "bpxwdyn failed with '%s' rc: '%d', emsg: '%s'", diag.service_name, rc, response.c_str());
      return RTNCD_FAILURE;
    }
  }

  return rc;
}

int zut_free_dynalloc_dds(ZDIAG &diag, const std::vector<std::string> &list)
{
  std::vector<std::string> free_dds;
  free_dds.reserve(list.size());

  for (const auto &entry : list)
  {
    std::string alloc_dd = entry;
    const auto dd_start = alloc_dd.find("dd(");
    if (dd_start == std::string::npos)
    {
      ZDIAG_SET_MSG(&diag, "Invalid format in DD alloc string: %s", entry.c_str());
      return RTNCD_FAILURE;
    }
    const auto paren_end = alloc_dd.find(")", dd_start + 3);
    if (paren_end == std::string::npos)
    {
      ZDIAG_SET_MSG(&diag, "Invalid format in DD alloc string: %s", entry.c_str());
      return RTNCD_FAILURE;
    }
    free_dds.push_back("free " + alloc_dd.substr(dd_start, paren_end - dd_start + 1));
  }

  return zut_loop_dynalloc(diag, free_dds);
}

AutocvtGuard::AutocvtGuard(bool enabled)
    : old_state(0)
{
  old_state = __ae_autoconvert_state(enabled ? _CVTSTATE_ON : _CVTSTATE_OFF);
}

AutocvtGuard::~AutocvtGuard()
{
  __ae_autoconvert_state(old_state);
}

FileGuard::FileGuard(const char *filename, const char *mode)
    : fp()
{
  fp = fopen(filename, mode);
}

FileGuard::FileGuard(int fd, const char *mode)
    : fp()
{
  fp = fdopen(fd, mode);
}

FileGuard::~FileGuard()
{
  if (fp)
  {
    fclose(fp);
  }
}

FileGuard::operator FILE *() const
{
  return fp;
}

FileGuard::operator bool() const
{
  return fp != nullptr;
}

std::string zut_read_input(std::istream &input_stream)
{
  std::string data;
  std::istreambuf_iterator<char> begin(input_stream);
  std::istreambuf_iterator<char> end;
  data.assign(begin, end);
  return data;
}

int zut_open_for_write(const std::string &path, mode_t mode, bool truncate)
{
  // One open() binds the existence check, creation, and truncation to the same object.
  // O_NOFOLLOW refuses a symbolic link as the last component, dangling or not.
  const int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | (truncate ? O_TRUNC : O_EXCL);
  const int fd = open(path.c_str(), flags, mode);
  if (fd == -1 && errno == EEXIST)
  {
    // O_EXCL may report a symbolic link as EEXIST before O_NOFOLLOW reports ELOOP.
    struct stat link_stats;
    const bool is_link = lstat(path.c_str(), &link_stats) == 0 && S_ISLNK(link_stats.st_mode);
    errno = is_link ? ELOOP : EEXIST;
  }
  return fd;
}

int zut_write_file_private(const std::string &path, const std::string &data, std::string &error)
{
  int fd = zut_open_for_write(path, S_IRUSR | S_IWUSR, true);
  if (fd == -1)
  {
    error = "could not open '" + path + "': " + (errno == ELOOP ? "path is a symbolic link" : strerror(errno));
    return RTNCD_FAILURE;
  }

  // O_CREAT's mode only applies when the file is newly created, so an existing
  // file with looser permissions needs to be tightened explicitly.
  if (fchmod(fd, S_IRUSR | S_IWUSR) == -1)
  {
    error = "could not set permissions on '" + path + "': " + strerror(errno);
    close(fd);
    return RTNCD_FAILURE;
  }

  const char *buf = data.data();
  size_t remaining = data.size();
  while (remaining > 0)
  {
    ssize_t written = write(fd, buf, remaining);
    if (written == -1)
    {
      if (errno == EINTR)
      {
        continue;
      }
      error = "could not write to '" + path + "': " + strerror(errno);
      close(fd);
      return RTNCD_FAILURE;
    }
    buf += written;
    remaining -= static_cast<size_t>(written);
  }

  if (close(fd) == -1)
  {
    error = "could not close '" + path + "': " + strerror(errno);
    return RTNCD_FAILURE;
  }

  return RTNCD_SUCCESS;
}

int zut_convert_date(const unsigned char *date_ptr, std::string &out_str)
{
  char buffer[12] = {0};

  int rc = ZUTCVTD(reinterpret_cast<const char *>(date_ptr), buffer);

  if (rc == 0)
  {
    out_str = buffer;
  }
  else
  {
    out_str = "";
  }

  return rc;
}

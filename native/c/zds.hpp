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

#ifndef ZDS_HPP
#define ZDS_HPP

#include <functional>
#include <vector>
#include <string>
#include "zdstype.h"
#include "zamtypes.h"

extern const size_t MAX_DS_LENGTH;

struct ZDSMem
{
  std::string name;
  int vers;
  int mod;
  std::string c4date;
  std::string m4date;
  std::string mtime;
  int cnorc;
  int inorc;
  int mnorc;
  std::string user;
  bool sclm;
  bool stats_valid;
};

struct ZDSEntry
{
  std::string name;
  long long alloc;
  int allocx;
  int blksize;
  std::string cdate;
  std::string dataclass;
  uint16_t devtype;
  std::string dsorg;
  std::string dsntype;
  std::string edate;
  bool encrypted;
  bool read_password;  // legacy MVS password required to read (DS1IND10 without DS1IND04)
  bool write_password; // legacy MVS password required to write (DS1IND10)
  int lrecl;
  std::string mgmtclass;
  bool migrated;
  bool multivolume;
  long long primary;
  std::string rdate;
  std::string recfm;
  long long secondary;
  std::string spacu;
  std::string storclass;
  int usedp;
  int usedx;
  std::string volser;
  std::vector<std::string> volsers;
  // ISPF shows the following fields, but we omit them since they require reading PDS directory (too slow)
  // Maximum dir. blocks, Used dir. blocks, Number of members
};

struct DS_ATTRIBUTES
{
  std::string alcunit;   // Allocation Unit
  int blksize;           // Block Size
  int dirblk;            // Directory Blocks
  std::string dsorg;     // Data Set Organization
  int primary;           // Primary Space
  std::string recfm;     // Record Format
  int lrecl;             // Record Length
  std::string dataclass; // Data Class
  std::string unit;      // Device Type
  std::string dsntype;   // Data Set Type
  std::string mgntclass; // Management Class
  std::string dsname;    // Data Set Name
  int avgblk;            // Average Block Length
  int secondary;         // Secondary Space
  int size;              // Size
  std::string storclass; // Storage Class
  std::string vol;       // Volume Serial
};

/**
 * Helper struct to hold data set attributes from DSCB lookup.
 */
struct DscbAttributes
{
  std::string recfm;
  int lrecl = 0;
  int blksize = 0;
  bool is_asa = false;
  bool read_password = false;
  bool write_password = false;
};

#ifdef SWIG
extern "C"
{
#endif

/**
 * @brief Options and results for data set copy operation
 */
struct ZDSCopyOptions
{
  bool replace;   // Replace flag for PS -> PS or replace like named members in target
  bool overwrite; // Overwrite entire target PDS

  bool target_exists;  // Set to true if target already exists
  bool member_created; // Set to true if target member was created

  ZDSCopyOptions()
      : replace(false), overwrite(false), target_exists(false), member_created(false)
  {
  }
};

/**
 * @brief Copy a data set or member
 *
 * @param zds data set returned attributes and error information
 * @param dsn1 source data set name
 * @param dsn2 destination data set name
 * @param options copy options and results (optional, uses defaults if nullptr)
 * @return int 0 for success; non zero otherwise
 */
int zds_copy_dsn(ZDS *zds, const std::string &dsn1, const std::string &dsn2, ZDSCopyOptions *options = nullptr);

/**
 * @brief Check if a data set exists
 *
 * @param dsn data set name to check
 * @return true if it exists; false otherwise
 */
bool zds_dataset_exists(const std::string &dsn);

/**
 * @brief Check if a data set member exists
 *
 * @param dsn data set name
 * @param member member name
 * @return true if it exists; false otherwise
 */
bool zds_member_exists(const std::string &dsn, const std::string &member);

/**
 * @brief Check if a string is a valid PDS/PDSE member name (up to 8 characters,
 *        alphabetic/national first character, alphanumeric/national thereafter)
 *
 * @param name member name to check
 * @return true if it is a syntactically valid member name; false otherwise
 */
bool zds_is_valid_member_name(const std::string &name);

/**
 * @brief Reject a data set protected by a legacy MVS password. OPEN on such a data
 *        set issues a WTOR and waits on the operator console, which hangs a caller
 *        with no console (e.g. zo server). Uses the VTOC (no OPEN), so call it before
 *        anything that opens the data set, including zds_dataset_exists.
 *
 * @param zds receives the diagnostic on failure
 * @param dsn data set name; a member name is ignored
 * @param for_write true to reject a write password as well as a read password
 * @return RTNCD_SUCCESS if the data set can be opened without a password prompt
 */
int zds_validate_no_password(ZDS *zds, const std::string &dsn, bool for_write);

/**
 * @brief Options for reading a z/OS data set or DD
 */
struct ZDSReadOpts
{
  ZDS *zds = nullptr;
  std::string ddname;
  std::string dsname;
  bool is_asa = false;

  // Invoked per chunk while streaming to signal that the request is still making
  // progress (e.g. to feed a caller's request-timeout watchdog). Lives here rather
  // than on ZDS because zdstype.h is also parsed by the Metal C compiler, which
  // cannot see std::function (see zam.c/zdsm.c).
  std::function<void()> update_heartbeat_callback;
};

/**
 * @brief Options for writing to a z/OS data set or DD
 */
struct ZDSWriteOpts
{
  ZDS *zds = nullptr;
  std::string ddname;
  std::string dsname;

  // Invoked per chunk while streaming; see ZDSReadOpts::update_heartbeat_callback
  std::function<void()> update_heartbeat_callback;
};

/**
 * @brief Write data to a z/OS data set or DD.
 *
 * @param opts write options containing ZDS state and either a dsname or ddname. If both ddname and dsname are provided, ddname takes precedence.
 * @param data data to write
 * @return int RTNCD_SUCCESS on success; RTNCD_FAILURE on failure
 */
int zds_write(const ZDSWriteOpts &opts, const std::string &data);

/**
 * @brief Write raw bytes to a data set with no code-page conversion or line
 *        splitting. Members go through BPAM (ENQ/RESERVE/STOW + DCB abend exit);
 *        sequential data sets use the binary fopen path, since BPAM output is
 *        PDS-only. The target must be V-format: fixed-format records are blank
 *        padded and would change the byte count.
 * @param opts write options; opts.dsname may name a member, e.g. MY.LIB(MEM)
 * @param data raw bytes to write
 * @return RTNCD_SUCCESS on success; RTNCD_FAILURE otherwise (details in zds->diag)
 */
int zds_write_binary(const ZDSWriteOpts &opts, const std::string &data);

/**
 * @brief Write data to a z/OS data set or DD in streaming mode.
 *
 * @param opts write options containing ZDS state and either a dsname or ddname. If both ddname and dsname are provided, ddname takes precedence.
 * @param pipe name of the input pipe
 * @param content_len pointer where the length of the data set contents will be stored
 * @return int RTNCD_SUCCESS on success; RTNCD_FAILURE on failure
 */
int zds_write_streamed(const ZDSWriteOpts &opts, const std::string &pipe, size_t *content_len);

/**
 * @brief Read data from a z/OS data set or DD.
 *
 * @param opts read options containing ZDS state and either a dsname or ddname. If both ddname and dsname are provided, ddname takes precedence.
 * @param response data read
 * @return int 0 for success; non zero otherwise
 */
int zds_read(const ZDSReadOpts &opts, std::string &response);

/**
 * @brief Open a data set for using BPAM mode
 *
 * @param zds data set returned attributes and error information
 * @param dsname data set name to open
 * @param ioc IO_CTRL pointer to the data set
 * @return int 0 for success; non zero otherwise
 */
int zds_open_output_bpam(ZDS *zds, const std::string &dsname, IO_CTRL *&ioc);

/**
 * @brief Write data to a data set using BPAM mode
 *
 * @param zds data set returned attributes and error information
 * @param ioc IO_CTRL pointer to the data set
 * @param data data to write
 * @param asa_char optional ASA control character to prepend (default '\0' means none)
 * @return int 0 for success; non zero otherwise
 */
int zds_write_output_bpam(ZDS *zds, IO_CTRL *ioc, std::string &data, char asa_char = '\0');

/**
 * @brief Close a data set using BPAM mode
 *
 * @param zds data set returned attributes and error information
 * @param ioc IO_CTRL pointer to the data set
 * @return int 0 for success; non zero otherwise
 */
int zds_close_output_bpam(ZDS *zds, IO_CTRL *ioc);

/**
 * @brief Create a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to create
 * @param response messages from dynamic allocation (which may be present even when successful requests are made)
 * @return int 0 for success; non zero otherwise
 */
int zds_create_dsn(ZDS *, const std::string &dsn, DS_ATTRIBUTES attributes, std::string &response);

/**
 * @brief Delete a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to delete to
 * @return int 0 for success; non zero otherwise
 */
int zds_delete_dsn(ZDS *zds, std::string dsn);

/**
 * @brief Rename a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn_Before data set name to rename
 * @param dsn_After new data set name
 * @return int 0 for success; non zero otherwise
 */
int zds_rename_dsn(ZDS *zds, std::string dsn_before, std::string dsn_after);

/**
 * @brief Rename a data set member
 *
 * @param zds
 * @param member_before
 * @param member_after
 * @return int 0 for success; non zero otherwise
 */
int zds_rename_members(ZDS *zds, const std::string &dsn, const std::string &member_before, const std::string &member_after);

/**
 * @brief Obtain list of members in a z/OS data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to obtain attributes for
 * @param members populated list returned containing member names within a z/OS data set
 * @param pattern optional wildcard pattern to filter members (supports '*' and '?')
 * @return int 0 for success; non zero otherwise
 */
int zds_list_members(ZDS *zds, std::string dsn, std::vector<ZDSMem> &members, const std::string &pattern = "", bool show_attributes = false);

int zds_list_data_sets(ZDS *zds, std::string dsn, std::vector<ZDSEntry> &datasets, bool show_attributes = false);
#ifdef SWIG
}
#endif

/**
 * @brief Read data from a DDNAME using ACB/RPL mode (for VSAM data sets)
 *
 * @param zds data set returned attributes and error information
 * @param ddname ddname from which to read
 * @param response data read
 * @return int 0 for success; non zero otherwise
 */
int zds_read_vsam(ZDS *zds, std::string ddname, std::string &response);

/**
 * @brief Create a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to create
 * @param response messages from dynamic allocation (which may be present even when successful requests are made)
 * @return int 0 for success; non zero otherwise
 */
int zds_create_dsn_fb(ZDS *zds, const std::string &dsn, std::string &response);

/**
 * @brief Create a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to create
 * @param response messages from dynamic allocation (which may be present even when successful requests are made)
 * @return int 0 for success; non zero otherwise
 */
int zds_create_dsn_vb(ZDS *zds, const std::string &dsn, std::string &response);

/**
 * @brief Create an ADATA data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to create
 * @param response messages from dynamic allocation (which may be present even when successful requests are made)
 * @return int 0 for success; non zero otherwise
 */
int zds_create_dsn_adata(ZDS *zds, const std::string &dsn, std::string &response);

/**
 * @brief Create a loadlib data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to create
 * @param response messages from dynamic allocation (which may be present even when successful requests are made)
 * @return int 0 for success; non zero otherwise
 */
int zds_create_dsn_loadlib(ZDS *zds, const std::string &dsn, std::string &response);

/**
 * @brief Delete a data set
 *
 * @param zds data set returned attributes and error information
 * @param dsn data set name to delete to
 * @return int 0 for success; non zero otherwise
 */
int zds_delete_dsn(ZDS *zds, std::string dsn);

int zdsReadDynalloc(const std::string &, const std::string &, const std::string &, std::string &); // NOTE(Kelosky): testing only

/**
 * @brief Read data from a z/OS data set in streaming mode.
 *
 * @param opts read options containing ZDS state and either a dsname or ddname. If both ddname and dsname are provided, ddname takes precedence.
 * @param pipe name of the output pipe
 * @param content_len pointer where the length of the data set contents will be stored
 * @param etag_checksum optional pointer where the Adler32 checksum of the raw content will be stored (nullptr to skip)
 * @return int 0 for success; non zero otherwise
 */
int zds_read_streamed(const ZDSReadOpts &opts, const std::string &pipe, size_t *content_len, uint32_t *etag_checksum = nullptr);

/**
 * @brief Invoke the IDCAMS program and receive its SYSPRINT output.
 * https://www.ibm.com/docs/en/zos-basic-skills?topic=management-access-method-services-idcams-commands
 * @param sysInData string data to write to the SYSIN DD for IDCAMS. Consists of commands to pass to IDCAMS.
 * @param output output from the SYSPRINT DD of IDCAMS will be written here.
 * @param error Reference to a string that receives a short error description on failure
 * @return Return code (0 for success, non-zero for error)
 */
int zds_idcams(const std::string &sysInData, std::string &output, std::string &error);

#endif

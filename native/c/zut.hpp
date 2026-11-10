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

#ifndef ZUT_HPP
#define ZUT_HPP

#include <sstream>
#include <ostream>
#include <iconv.h>
#include <vector>
#include <optional>
#include <string>
#include <sys/types.h>
#include "ztype.h"
#include "zutm.h"

/**
 * @struct ZConvData
 * @brief Structure holding data for character set conversion
 */
struct ZConvData
{
  char *input;            /**< Pointer to input buffer. */
  size_t input_size;      /**< Size of input buffer. */
  size_t max_output_size; /**< Maximum size of output buffer. */
  char *output_buffer;    /**< Pointer to output buffer. */
  char *output_iter;      /**< Pointer to current position in output buffer. */
};

/**
 * @brief Structure holding alternate DD names for DFSMSdfp utilities.
 */
struct DFSMSdfp_AltDDs
{
  const std::string &sysin;
  const std::string &sysprint;
  const std::string &sysut1;
  const std::string &sysut2;
  std::optional<std::string> sysut3;
  std::optional<std::string> sysut4;
};

/**
 * @brief Run IEBCOPY utility
 * @param diag Reference to diagnostic information structure
 * @param opts Options string
 * @param options Pointer to DFSMSdfp_DD_Options structure
 * @return Return code (0 for success, non-zero for error)
 */
int zut_iebcopy(ZDIAG &diag, const std::string &opts, const DFSMSdfp_AltDDs *options);

/**
 * @brief Run IEBGENER utility
 * @param diag Reference to diagnostic information structure
 * @param opts Options string
 * @param options Pointer to DFSMSdfp_ALT_DDS structure
 * @return Return code (0 for success, non-zero for error)
 */
int zut_iebgener(ZDIAG &diag, const std::string &opts, const DFSMSdfp_AltDDs *options);

/**
 * @brief Strips the last character from input if it's a newline.
 * @param input The string to modify
 * @returns Nothing. The input is modified if its last character is a newline.
 */
void zut_strip_final_newline(std::string &input);

/**
 * @brief Runs a program
 * @param program The program to run. The program must be on PATH or a fully-qualified path to the executable
 * @param args Arguments passed to the program
 * @param response Reference to a string where the program's combined stdout and stderr content will be stored
 * @returns The return code from running the command, or non-zero for error submitting
 */
int zut_run_program(const std::string &program, const std::vector<std::string> &args, std::string &response);

/**
 * @brief Runs a program
 * @param program The program to run. The program must be on PATH or a fully-qualified path to the executable
 * @param args Arguments passed to the program
 * @param stdout_response Reference to a string where the program's stdout content will be stored.
 * @param stderr_response Reference to a string where the program's stderr content will be stored.
 * @returns The return code from running the command, or non-zero for error submitting
 */
int zut_run_program(const std::string &program, const std::vector<std::string> &args, std::string &stdout_response, std::string &stderr_response);

/**
 * @brief Runs a shell command using spawn() with _BPX_SHAREAS=YES for efficient same-address-space execution.
 * @param command The shell command to execute (passed to /bin/sh -c)
 * @param stdout_response Reference to a string where the command's stdout will be stored
 * @param stderr_response Reference to a string where the command's stderr will be stored
 * @returns The exit code from the shell command, or non-zero for spawn/wait errors
 */
int zut_spawn_shell_command(const std::string &command, std::string &stdout_response, std::string &stderr_response);

/**
 * @brief Search for a specific string
 * @param input The string to search for
 * @return Return code (0 for success, non-zero for error)
 */
int zut_search(const std::string &input);

/**
 * @brief Run a specified command or operation
 * @param diag Reference to diagnostic information structure
 * @param input The command string to execute
 * @param parms The parameters string to execute
 * @return Return code (0 for success, non-zero for error)
 */
int zut_run(const std::string &program);

/**
 * @brief Run a specified command or operation
 * @param diag Reference to diagnostic information structure
 * @param input The command string to execute
 * @param parms The parameters string to execute
 * @return Return code (0 for success, non-zero for error)
 */
int zut_run(ZDIAG &diag, const std::string &input, const std::string &parms);

/**
 * @brief Set a DD name in char array with padded spaces, or binary zeros for null.
 * @param arr The 8-byte character array to set
 * @param ddname The DD name to set
 */
void zut_set_dd_with_padding(char arr[8], const char *ddname);

/**
 * @brief Substitute a symbol in a string
 * @param symbol The symbol to substitute
 * @param result Reference to a string where the result will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_substitute_symbol(const std::string &symbol, std::string &result);

/**
 * @brief Invoke BPXWDYN service with the given parameters
 * @param command The command string
 * @param code Pointer to return code (output)
 * @param resp Reference to a string where the result will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_bpxwdyn(const std::string &command, unsigned int *code, std::string &resp);

/**
 * @brief Invoke BPXWDYN service with the given parameters and return the DD or DS name
 * @param command The command string
 * @param code Pointer to return code (output)
 * @param resp Reference to a string where the result will be stored
 * @param ddname Reference to a string where the DD name will be stored
 * @param dsname Reference to a string where the DS name will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_bpxwdyn_common(const std::string &command, unsigned int *code, std::string &resp, std::string &ddname, std::string &dsname);

/**
 * @brief Invoke BPXWDYN service with the given parameters and return the DD name
 * @param command The command string
 * @param code Pointer to return code (output)
 * @param resp Reference to a string where the result will be stored
 * @param ddname Reference to a string where the DD name will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_bpxwdyn_rtdd(const std::string &command, unsigned int *code, std::string &resp, std::string &ddname);

/**
 * @brief Invoke BPXWDYN service with the given parameters and return the DS name
 * @param command The command string
 * @param code Pointer to return code (output)
 * @param resp Reference to a string where the result will be stored
 * @param dsname Reference to a string where the DS name will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_bpxwdyn_rtdsn(const std::string &command, unsigned int *code, std::string &resp, std::string &dsname);

/**
 * @brief Print a hello message
 * @param input Input string for the hello message
 * @return Return code (0 for success, non-zero for error)
 */
int zut_hello(const std::string &input);

/**
 * @brief Convert an integer to its hexadecimal character representation
 * @param value Integer value
 * @return Corresponding hexadecimal character
 */
char zut_get_hex_char(int value);

/**
 * @brief Get the current user name
 * @param user Reference to a string where the user name will be stored
 * @return Return code (0 for success, non-zero for error)
 */
int zut_get_current_user(std::string &user);

/**
 * @brief Convert a string to uppercase, pad or truncate as needed
 * @param dest Destination buffer
 * @param src Source string
 * @param length Desired length of the output
 */
void zut_uppercase_pad_truncate(char *dest, std::string src, int length);

/**
 * @brief Convert a DSECT
 * @return Return code (0 for success, non-zero for error)
 */
int zut_convert_dsect();

/**
 * @brief Prepare encoding options
 * @param encoding_value The encoding to use
 * @param opts Pointer to encoding options structure
 * @return True if preparation is successful, false otherwise
 */
bool zut_prepare_encoding(const std::string &encoding_value, ZEncode *opts);

/**
 * @brief Print a string as a sequence of bytes
 * @param input The string to print
 * @param output_stream Pointer to output stream (defaults to std::cout)
 */
void zut_print_string_as_bytes(std::string &input, std::ostream *out_stream = nullptr);

/**
 * @brief Convert a hexadecimal string to a vector of bytes
 * @param hex_string The hexadecimal string
 * @return Vector containing the bytes
 */
std::vector<uint8_t> zut_get_contents_as_bytes(const std::string &hex_string);

/**
 * @brief State for incremental Adler-32 checksum computation
 */
struct Adler32State
{
  uint32_t a = 1u;
  uint32_t b = 0u;
};

/**
 * @brief Update Adler-32 state with new data incrementally
 * @param state The current Adler-32 state
 * @param data Pointer to the data
 * @param len Length of the data
 */
void zut_adler32_update(Adler32State &state, const char *data, size_t len);

/**
 * @brief Finalize the Adler-32 checksum from the given state
 * @param state The Adler-32 state
 * @return The final Adler-32 checksum
 */
uint32_t zut_adler32_finalize(const Adler32State &state);

/**
 * @brief Calculate the Adler-32 checksum of a string
 * @param input The input string
 * @return The Adler-32 checksum
 */
uint32_t zut_calc_adler32_checksum(const std::string &input);

/**
 * @brief Perform character set conversion using iconv
 * @param cd iconv conversion descriptor
 * @param data Reference to ZConvData containing buffers and sizes
 * @param diag Reference to diagnostic information structure
 * @param flush_state If true, flush the shift state for stateful encodings (e.g., IBM-939). Set to true on the last chunk.
 * @return Number of bytes converted or error code
 */
size_t zut_iconv(iconv_t cd, ZConvData &data, ZDIAG &diag, bool flush_state = true);

/**
 * @brief Flush the shift state for stateful encodings (e.g., IBM-939) - simplified version
 * @param cd iconv conversion descriptor
 * @param diag Reference to diagnostic information structure
 * @return Vector containing the flushed bytes (empty on error)
 */
std::vector<char> zut_iconv_flush(iconv_t cd, ZDIAG &diag);

/**
 * @brief Build an ETag string from file modification time and size
 * @param mtime Modification time
 * @param byte_size File size in bytes
 * @return Generated ETag string
 */
std::string zut_build_etag(const size_t mtime, const size_t byte_size);

/**
 * @brief Encode a string from one character set to another
 * @param input_str The input string
 * @param from_encoding Source encoding name
 * @param to_encoding Target encoding name
 * @param diag Reference to diagnostic information structure
 * @return The encoded string
 */
std::string zut_encode(const std::string &input_str, const std::string &from_encoding, const std::string &to_encoding, ZDIAG &diag);

std::vector<char> zut_encode(const char *input_str, const size_t input_size, const std::string &from_encoding, const std::string &to_encoding, ZDIAG &diag);

/**
 * @brief Encode a string using an existing iconv descriptor
 * @param input_str The input string
 * @param cd iconv descriptor (caller manages opening, flushing, and closing)
 * @param diag Reference to diagnostic information structure
 * @param flush_state If true, flush the shift state after this input so the
 *   returned bytes are self-contained (e.g. a stateful mixed SBCS/DBCS encoding
 *   ends back in single-byte state). Use this when each call produces an
 *   independent unit such as a single data set record. Defaults to false so the
 *   shift state carries across successive calls on the same descriptor.
 * @return The encoded string
 */
std::string zut_encode(const std::string &input_str, iconv_t cd, ZDIAG &diag, bool flush_state = false);

std::vector<char> zut_encode(const char *input_str, const size_t input_size, iconv_t cd, ZDIAG &diag, bool flush_state = false);

/**
 * @brief Format a vector of strings as a CSV line
 * @param fields Vector of fields
 * @return CSV-formatted string
 */
std::string zut_format_as_csv(std::vector<std::string> &fields);

/**
 * @brief Convert an integer to an uppercase hexadecimal string
 * @param value The integer value to convert
 * @return The uppercase hexadecimal string representation of the integer
 */
template <typename T>
inline std::string zut_hex_to_string(T value)
{
  std::ostringstream oss;
  oss << std::hex << std::uppercase << value;
  return oss.str();
}

/**
 * @brief Trim whitespace from the right end of a string
 * @param s String to trim
 * @param t Characters to trim (default is space)
 * @return Reference to the trimmed string
 */
std::string &zut_rtrim(std::string &s, const char *t = " ");

/**
 * @brief Trim whitespace from the left end of a string
 * @param s String to trim
 * @param t Characters to trim (default is space)
 * @return Reference to the trimmed string
 */
std::string &zut_ltrim(std::string &s, const char *t = " ");

/**
 * @brief Trim whitespace from both ends of a string
 * @param s String to trim
 * @param t Characters to trim (default is space)
 * @return Reference to the trimmed string
 */
std::string &zut_trim(std::string &s, const char *t = " ");

/**
 * @brief Get current PSW key
 *
 * @return unsigned char The current PSW key
 */
unsigned char zut_get_key();

/**
 * @brief Default debug message function for zut_dump_storage
 *
 * @param message Message to be printed
 */
void zut_debug_message(const char *message);

/**
 * @brief String comparison function using C strcmp for sorting
 * @param a First string to compare
 * @param b Second string to compare
 * @return True if a should come before b in sorted order, false otherwise
 */
bool zut_string_compare_c(const std::string &a, const std::string &b);

/**
 * @brief Loop through a list of dynamic allocation commands and call BPXWDYN for each one
 * @param list List of dynamic allocation commands
 * @return Return code (0 for success, non-zero for error)
 */
int zut_loop_dynalloc(ZDIAG &diag, const std::vector<std::string> &list);

/**
 * @brief Free a list of dynamic allocation commands
 * @param list List of dynamic allocation commands
 * @param diag Reference to diagnostic information structure
 * @return Return code (0 for success, non-zero for error)
 */
int zut_free_dynalloc_dds(ZDIAG &diag, const std::vector<std::string> &list);

/**
 * @brief List APF using ZUTMAPFQ
 * @param diag Reference to diagnostic information structure
 * @param apf Reference to vector of APF names
 * @return Return code (0 for success, non-zero for error)
 */
int zut_list_apf(ZDIAG &diag, std::vector<std::pair<std::string, std::string>> &apf);

/**
 * @brief A single data set entry in a LNKLST set, as returned by @ref zut_list_linklist
 */
struct ZLinklstEntry
{
  std::string dsname;
  std::string volume;
  bool apf;
};

/**
 * @brief List data sets in the currently active link list (LNKLST) set using ZUTMDYNQ (CSVDYNL REQUEST=LIST)
 * @param diag Reference to diagnostic information structure
 * @param linklist Reference to vector of link list data set entries
 * @return Return code (0 for success, non-zero for error)
 */
int zut_list_linklist(ZDIAG &diag, std::vector<ZLinklstEntry> &linklist);

/**
 * @brief List a parmlib
 * @param diag Reference to diagnostic information structure
 * @param parmlibs Reference to vector of parmlib names
 * @return Return code (0 for success, non-zero for error)
 */
int zut_list_parmlib(ZDIAG &diag, std::vector<std::string> &parmlibs);

/**
 * @brief List subsystems using IEFSSI_QUERY
 * @param diag Reference to diagnostic information structure
 * @param subsystems Reference to vector of subsystem names
 * @return Return code (0 for success, non-zero for error)
 */
int zut_list_subsystems(ZDIAG &diag, std::vector<std::string> &subsystems, std::string filter = "*");

/**
 * @brief Read input data from a stream, handling both TTY and piped input
 *
 * When stdin is not a TTY (piped input), reads raw bytes using istreambuf_iterator.
 * When stdin is a TTY, reads lines using getline and preserves newlines between lines.
 *
 * @param input_stream The input stream to read from
 * @return The data read from the input stream
 */
std::string zut_read_input(std::istream &input_stream);

/**
 * @brief Open a file for writing without ever following a symbolic link as the
 *   last path component.
 *
 * A single open() with O_CREAT | O_NOFOLLOW binds the existence check, creation,
 * and truncation to the same object, so neither a dangling nor a live symbolic
 * link can redirect the write. Without `truncate` the file is created with O_EXCL
 * and any existing path fails.
 *
 * @param path Path of the file to open
 * @param mode Permission bits for a newly created file (subject to the umask)
 * @param truncate Open and truncate an existing regular file instead of failing with EEXIST
 * @return File descriptor, or -1 with errno set; ELOOP when `path` is a symbolic link
 */
int zut_open_for_write(const std::string &path, mode_t mode, bool truncate);

/**
 * @brief Atomically replace a file with owner-only read/write permissions (0600),
 *   regardless of the process umask.
 *
 * Intended for output that may hold secrets, such as an exported private key
 * (e.g. a PKCS#12 bundle). Existing symbolic-link destinations are rejected.
 * Data is written to an exclusively created temporary file in the same directory,
 * validated, and closed before renaming it over the destination. Failed writes
 * leave the previous output intact; hard-linked files are not modified in place.
 *
 * @param path Path of the file to create or replace
 * @param data The bytes to write
 * @param error Reference to a string that receives a short error description on failure
 * @return Return code (0 for success, non-zero for error)
 */
int zut_write_file_private(const std::string &path, const std::string &data, std::string &error);

int zut_convert_date(const unsigned char *date_ptr, std::string &out_str);

/**
 * @brief RAII class to manage auto-conversion state
 *
 * Saves the current auto-conversion state on construction and restores it on destruction.
 * This ensures that any changes to the auto-conversion state are properly reverted.
 */
class AutocvtGuard
{
  int old_state;

public:
  AutocvtGuard(bool enabled);
  ~AutocvtGuard();
};

/**
 * @brief RAII class to manage FILE* pointers
 *
 * Opens a file on construction and automatically closes it on destruction.
 * Provides implicit conversion to FILE* for easy use with C file APIs.
 */
class FileGuard
{
  FILE *fp;

public:
  FileGuard(const char *filename, const char *mode);
  FileGuard(int fd, const char *mode);
  ~FileGuard();

  // Delete copy and move since ownership is non-transferable
  FileGuard(const FileGuard &) = delete;
  FileGuard &operator=(const FileGuard &) = delete;
  FileGuard(FileGuard &&) = delete;
  FileGuard &operator=(FileGuard &&) = delete;

  // Allow reassignment via reset
  void reset(const char *filename, const char *mode);
  void reset(int fd, const char *mode);
  void reset();

  operator FILE *() const;
  operator bool() const;
};

/**
 * RAII helper class to preserve diagnostic message across operations that may overwrite it.
 * Saves the current e_msg on construction and restores it on destruction.
 */
class DiagMsgGuard
{
  ZDIAG *diag_;
  char saved_msg_[256];
  int saved_msg_len_;

public:
  explicit DiagMsgGuard(ZDIAG *diag)
      : diag_(diag), saved_msg_len_(diag->e_msg_len)
  {
    memcpy(saved_msg_, diag->e_msg, sizeof(saved_msg_));
  }

  ~DiagMsgGuard()
  {
    memcpy(diag_->e_msg, saved_msg_, sizeof(saved_msg_));
    diag_->e_msg_len = saved_msg_len_;
  }

  // Non-copyable
  DiagMsgGuard(const DiagMsgGuard &) = delete;
  DiagMsgGuard &operator=(const DiagMsgGuard &) = delete;
};

/**
 * RAII helper class to manage iconv descriptor lifecycle.
 * Automatically closes the iconv descriptor on destruction, ensuring proper cleanup
 * even in error paths.
 */
class IconvGuard
{
  iconv_t cd_;

public:
  IconvGuard()
      : cd_((iconv_t)(-1))
  {
  }

  explicit IconvGuard(const char *to_code, const char *from_code)
      : cd_((to_code && from_code) ? iconv_open(to_code, from_code) : (iconv_t)(-1))
  {
  }

  ~IconvGuard()
  {
    if (cd_ != (iconv_t)(-1))
    {
      iconv_close(cd_);
    }
  }

  iconv_t get() const
  {
    return cd_;
  }

  bool is_valid() const
  {
    return cd_ != (iconv_t)(-1);
  }

  // Non-copyable
  IconvGuard(const IconvGuard &) = delete;
  IconvGuard &operator=(const IconvGuard &) = delete;
};

/**
 * Helper struct to track truncated lines with range compression.
 * Consecutive lines are compressed into ranges (e.g., "5-8, 12, 45-46").
 */
struct TruncationTracker
{
  int count;
  int range_start;
  int range_end;
  std::string lines_str;

  TruncationTracker()
      : count(0), range_start(-1), range_end(-1), lines_str("")
  {
    lines_str.reserve(128);
  }

  void flush_range()
  {
    if (range_start == -1)
      return;

    if (!lines_str.empty())
      lines_str += ", ";

    std::ostringstream ss;
    if (range_start == range_end)
    {
      ss << range_start;
    }
    else
    {
      ss << range_start << "-" << range_end;
    }
    lines_str += ss.str();
    range_start = range_end = -1;
  }

  void add_line(int line_num)
  {
    count++;
    if (range_start == -1)
    {
      range_start = range_end = line_num;
    }
    else if (line_num == range_end + 1)
    {
      range_end = line_num;
    }
    else
    {
      flush_range();
      range_start = range_end = line_num;
    }
  }

  std::string get_warning_message() const
  {
    std::ostringstream ss;
    ss << count << " line(s) truncated to fit LRECL: " << lines_str;
    return ss.str();
  }
};

#endif // ZUT_HPP

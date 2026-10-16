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

#include "zusf_py.hpp"

void handle_zusf_error(const ZUSF &ctx, int rc)
{
  if (rc != 0)
  {
    std::string diag(ctx.diag.e_msg, ctx.diag.e_msg_len);
    e2a_inplace(diag);
    throw std::runtime_error(diag);
  }
}

/** Records the requested codepage, comparing "binary" before converting the name. */
void set_encoding_opts(ZUSF &ctx, const std::string &codepage)
{
  if (codepage.empty())
  {
    return;
  }

  ctx.encoding_opts.data_type = codepage == "binary" ? eDataTypeBinary : eDataTypeText;
  std::string encoded = codepage;
  a2e_inplace(encoded);
  size_t max_len = sizeof(ctx.encoding_opts.codepage) - 1;
  strncpy(ctx.encoding_opts.codepage, encoded.c_str(), max_len);
  ctx.encoding_opts.codepage[max_len] = '\0';
}

void set_etag(ZUSF &ctx, const std::string &etag)
{
  if (etag.empty())
  {
    return;
  }

  std::string encoded = etag;
  a2e_inplace(encoded);
  size_t max_len = sizeof(ctx.etag) - 1;
  strncpy(ctx.etag, encoded.c_str(), max_len);
  ctx.etag[max_len] = '\0';
}

std::string get_etag(const ZUSF &ctx)
{
  std::string etag(ctx.etag);
  e2a_inplace(etag);
  return etag;
}

void create_uss_file(const std::string &file, const std::string &mode)
{
  ZUSF ctx = {0};
  mode_t octal_mode = std::stoi(mode, nullptr, 8);
  std::string path = file;
  a2e_inplace(path);
  handle_zusf_error(ctx, zusf_create_uss_file_or_dir(&ctx, path, octal_mode, CreateOptions(false)));
}

void create_uss_dir(const std::string &file, const std::string &mode)
{
  ZUSF ctx = {0};
  mode_t octal_mode = std::stoi(mode, nullptr, 8);
  std::string path = file;
  a2e_inplace(path);
  handle_zusf_error(ctx, zusf_create_uss_file_or_dir(&ctx, path, octal_mode, CreateOptions(true)));
}

void move_uss_file_or_dir(const std::string &source, const std::string &destination)
{
  ZUSF ctx = {0};
  std::string from = source;
  std::string to = destination;
  a2e_inplace(from);
  a2e_inplace(to);
  handle_zusf_error(ctx, zusf_move_uss_file_or_dir(&ctx, from, to));
}

std::string list_uss_dir(const std::string &path, ListOptions options)
{
  ZUSF ctx = {0};
  std::string dir = path;
  a2e_inplace(dir);

  std::string out;
  handle_zusf_error(ctx, zusf_list_uss_file_path(&ctx, dir, out, options));

  e2a_inplace(out);
  return out;
}

std::string read_uss_file(const std::string &file, const std::string &codepage)
{
  ZUSF ctx = {0};
  set_encoding_opts(ctx, codepage);

  std::string path = file;
  a2e_inplace(path);

  // zusf owns the content encoding: it converts between the file's codepage and UTF-8, so the
  // payload crosses this boundary already in the encoding Python expects.
  std::string response;
  handle_zusf_error(ctx, zusf_read_from_uss_file(&ctx, path, response));

  return response;
}

void read_uss_file_streamed(const std::string &file, const std::string &pipe, const std::string &codepage, size_t *content_len)
{
  size_t bytes_read = 0;
  if (content_len == nullptr)
    content_len = &bytes_read;

  ZUSF ctx = {0};
  set_encoding_opts(ctx, codepage);

  std::string path = file;
  std::string pipe_path = pipe;
  a2e_inplace(path);
  a2e_inplace(pipe_path);

  handle_zusf_error(ctx, zusf_read_from_uss_file_streamed(&ctx, path, pipe_path, content_len));
}

std::string write_uss_file(const std::string &file, const std::string &data, const std::string &codepage, const std::string &etag)
{
  ZUSF ctx = {0};
  set_encoding_opts(ctx, codepage);
  set_etag(ctx, etag);

  std::string path = file;
  std::string contents = data; // zusf_write_to_uss_file takes a mutable reference
  a2e_inplace(path);

  handle_zusf_error(ctx, zusf_write_to_uss_file(&ctx, path, contents));

  return get_etag(ctx);
}

std::string write_uss_file_streamed(const std::string &file, const std::string &pipe, const std::string &codepage, const std::string &etag, size_t *content_len)
{
  size_t bytes_written = 0;
  if (content_len == nullptr)
    content_len = &bytes_written;

  ZUSF ctx = {0};
  set_encoding_opts(ctx, codepage);
  set_etag(ctx, etag);

  std::string path = file;
  std::string pipe_path = pipe;
  a2e_inplace(path);
  a2e_inplace(pipe_path);

  handle_zusf_error(ctx, zusf_write_to_uss_file_streamed(&ctx, path, pipe_path, content_len));

  return get_etag(ctx);
}

void chmod_uss_item(const std::string &file, const std::string &mode, bool recursive)
{
  ZUSF ctx = {0};
  mode_t octal_mode = std::stoi(mode, nullptr, 8);
  std::string path = file;
  a2e_inplace(path);
  handle_zusf_error(ctx, zusf_chmod_uss_file_or_dir(&ctx, path, octal_mode, recursive));
}

void delete_uss_item(const std::string &file, bool recursive)
{
  ZUSF ctx = {0};
  std::string path = file;
  a2e_inplace(path);
  handle_zusf_error(ctx, zusf_delete_uss_item(&ctx, path, recursive));
}

void chown_uss_item(const std::string &file, const std::string &owner, bool recursive)
{
  ZUSF ctx = {0};
  std::string path = file;
  std::string new_owner = owner;
  a2e_inplace(path);
  a2e_inplace(new_owner);
  handle_zusf_error(ctx, zusf_chown_uss_file_or_dir(&ctx, path, new_owner, recursive));
}

void chtag_uss_item(const std::string &file, const std::string &tag, bool recursive)
{
  ZUSF ctx = {0};
  std::string path = file;
  std::string new_tag = tag;
  a2e_inplace(path);
  a2e_inplace(new_tag);
  handle_zusf_error(ctx, zusf_chtag_uss_file_or_dir(&ctx, path, new_tag, recursive));
}

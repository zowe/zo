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

#ifndef RESULT_TABLE_HPP
#define RESULT_TABLE_HPP

#include "../extend/plugin.hpp"
#include "../zut.hpp"
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

namespace commands
{
namespace format
{

class ResultTable;

/**
 * @brief One row under construction; obtain it from ResultTable::row()
 */
class Row
{
public:
  Row &add(const std::string &value)
  {
    m_cells.push_back(value);
    return *this;
  }

  Row &add(const char *value)
  {
    m_cells.push_back(value != nullptr ? value : "");
    return *this;
  }

  Row &add(long long value)
  {
    m_cells.push_back(std::to_string(value));
    return *this;
  }

  Row &add(int value)
  {
    return add(static_cast<long long>(value));
  }

  /**
   * @brief Add a numeric cell, blank when the value is the "unset" sentinel
   *
   * The z/OS control blocks report a missing attribute as -1, which the table
   * shows as an empty column rather than as "-1".
   */
  Row &add_or_blank(long long value, long long sentinel = -1)
  {
    if (value == sentinel)
    {
      m_cells.push_back("");
      return *this;
    }
    return add(value);
  }

  /**
   * @brief Add a boolean cell rendered with the command's own wording
   */
  Row &add_flag(bool value, const char *when_true, const char *when_false)
  {
    return add(value ? when_true : when_false);
  }

  /**
   * @brief Render this row and record its structured form
   *
   * @param object Structured representation of the row, appended to the items
   *               array; pass nothing for a command that reports a single
   *               object and builds it itself
   */
  void emit(const ast::Node &object = ast::Node());

private:
  friend class ResultTable;

  explicit Row(ResultTable &table)
      : m_table(table)
  {
  }

  Row(const Row &) = delete;
  Row &operator=(const Row &) = delete;

  ResultTable &m_table;
  std::vector<std::string> m_cells;
};

/**
 * @brief Renders a command's tabular result once, in whichever text form was asked for
 *
 * Every list-style handler used to spell its fields out twice: once as
 * `fields.push_back(...)` for `--response-format-csv` and once as a
 * `std::setw(...)` chain for the human-readable table, with a third,
 * independent pass building the ast::Node for `--json` and the RPC server.
 * The two text renderings had to be kept in step by hand, and neither one
 * shared a definition of how wide a column is.
 *
 * ResultTable owns the text side: declare the named columns once with
 * add_column(), then feed each row through row(). Whether that row lands as a
 * padded table line or a CSV record is decided here, from the
 * `response-format-csv` argument, rather than at every call site. Rows are
 * written as they arrive, so a large listing still streams instead of
 * accumulating in memory.
 *
 * Naming a column also makes it printable: passing `response-format-header`
 * prints the column names as the first line, ahead of the first row, in
 * whichever of the two text forms was asked for.
 *
 * The JSON side is still supplied per row, as an ast::Node handed to
 * Row::emit(). The structured output is deliberately not derived from the
 * columns: it carries fields the table never shows (a data set's dataclass or
 * storclass, a job's phase and correlator) and it omits absent values instead
 * of rendering them as an empty column.
 *
 * A row may supply fewer cells than there are columns. The table prints only
 * the cells given -- matching, for instance, a PDS member whose statistics are
 * missing -- while CSV pads out to the full column count so that every record
 * has the same shape.
 */
class ResultTable
{
public:
  explicit ResultTable(plugin::InvocationContext &context)
      : m_context(context),
        m_csv(context.get<bool>("response-format-csv", false)),
        m_header(context.get<bool>("response-format-header", false)),
        m_header_printed(false),
        m_items(ast::arr()),
        m_row(*this)
  {
  }

  /**
   * @brief Declare the next column, in display order
   *
   * @param name  Column name, shown as its heading when a caller asks for
   *              `response-format-header`
   * @param width Column width for the human-readable table; 0 leaves the cell
   *              unpadded, which is what the final column of a row wants.
   *              Ignored in CSV, where the width only fixes the field count.
   */
  ResultTable &add_column(const std::string &name, int width = 0)
  {
    m_names.push_back(name);
    m_widths.push_back(width);
    return *this;
  }

  /**
   * @brief Start a new row, discarding any cells left from the previous one
   */
  Row &row()
  {
    m_row.m_cells.clear();
    return m_row;
  }

  /**
   * @brief Publish the collected rows as the command's structured result
   *
   * @param items_key       Key the array is stored under
   * @param include_row_count Whether to add the "returnedRows" count alongside
   */
  void finish(const std::string &items_key = "items", bool include_row_count = false)
  {
    const auto result = ast::obj();
    result->set(items_key, m_items);
    if (include_row_count)
    {
      result->set("returnedRows", ast::i64(static_cast<long long>(m_items->as_array().size())));
    }
    m_context.set_object(result);
  }

private:
  friend class Row;

  void print_line(const std::vector<std::string> &cells)
  {
    std::ostream &out = m_context.output_stream();

    if (m_csv)
    {
      // zut_format_as_csv trims each field and takes a mutable reference, so
      // the padded copy is made here rather than shared with the caller.
      std::vector<std::string> fields(cells);
      if (fields.size() < m_widths.size())
      {
        fields.resize(m_widths.size());
      }
      out << zut_format_as_csv(fields) << std::endl;
      return;
    }

    out << std::left;
    for (size_t i = 0; i < cells.size(); ++i)
    {
      if (i > 0)
      {
        out << " ";
      }
      const int width = i < m_widths.size() ? m_widths[i] : 0;
      if (width > 0)
      {
        out << std::setw(width);
      }
      out << cells[i];
    }
    out << std::endl;
  }

  void emit_row(const std::vector<std::string> &cells, const ast::Node &object)
  {
    if (object)
    {
      m_items->push(object);
    }

    if (m_header && !m_header_printed)
    {
      m_header_printed = true;
      print_line(m_names);
    }

    print_line(cells);
  }

  plugin::InvocationContext &m_context;
  bool m_csv;
  bool m_header;
  bool m_header_printed;
  std::vector<std::string> m_names;
  std::vector<int> m_widths;
  ast::Node m_items;
  Row m_row;
};

inline void Row::emit(const ast::Node &object)
{
  m_table.emit_row(m_cells, object);
}

} // namespace format
} // namespace commands

#endif // RESULT_TABLE_HPP

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

#include <sstream>
#include <string>
#include <vector>

#include "result_table.test.hpp"
#include "ztest.hpp"
#include "../commands/result_table.hpp"

using namespace ztst;
using namespace commands::format;

namespace
{

/**
 * @brief An InvocationContext writing to a buffer, with output format preset
 *
 * The golden strings below are the exact bytes the ds/job handlers produced
 * before they moved onto ResultTable, so a change in padding or field count
 * shows up here rather than in a z/OSMF client.
 */
class TableFixture
{
public:
  explicit TableFixture(bool csv, bool header = false)
      : m_args(make_args(csv, header)),
        m_context_args("test", m_args, std::vector<std::string>(), nullptr, &m_out, nullptr), m_context(m_context_args),
        table(m_context)
  {
  }

  std::string output() const
  {
    return m_out.str();
  }

  plugin::InvocationContext &context()
  {
    return m_context;
  }

private:
  static plugin::ArgumentMap make_args(bool csv, bool header)
  {
    plugin::ArgumentMap args;
    args["response-format-csv"] = plugin::Argument(csv);
    args["response-format-header"] = plugin::Argument(header);
    return args;
  }

  plugin::ArgumentMap m_args;
  std::stringstream m_out;
  plugin::ContextArgs m_context_args;
  plugin::InvocationContext m_context;

public:
  ResultTable table;
};

} // namespace

void result_table_tests()
{
  describe("result table tests", []() -> void
  {
    describe("human-readable table", []() -> void
    {
      it("pads a single column to its declared width", []()
      {
        TableFixture fixture(false);
        fixture.table.add_column("dsname", 44);
        fixture.table.row().add("SYS1.PARMLIB").emit();

        Expect(fixture.output()).ToBe(std::string("SYS1.PARMLIB") + std::string(32, ' ') + "\n");
      });

      it("separates columns with a single space and leaves the last unpadded", []()
      {
        TableFixture fixture(false);
        fixture.table.add_column("ddname", 9)
            .add_column("dsname")
            .add_column("id", 4)
            .add_column("stepname")
            .add_column("procstep");
        fixture.table.row().add("JESMSGLG").add("JES2.DDNAME").add(2).add("STEP1").add("").emit();

        Expect(fixture.output()).ToBe(std::string("JESMSGLG  JES2.DDNAME 2    STEP1 \n"));
      });

      it("prints only the cells a short row supplies", []()
      {
        // A PDS member without valid statistics: the name alone, padded, with
        // no empty attribute columns trailing it.
        TableFixture fixture(false);
        fixture.table.add_column("member", 12).add_column("vers", 4).add_column("mod", 4);
        fixture.table.row().add("MEMBER1").emit();

        Expect(fixture.output()).ToBe(std::string("MEMBER1") + std::string(5, ' ') + "\n");
      });

      it("does not truncate a cell wider than its column", []()
      {
        TableFixture fixture(false);
        fixture.table.add_column("id", 4).add_column("name");
        fixture.table.row().add("LONGVALUE").add("tail").emit();

        Expect(fixture.output()).ToBe(std::string("LONGVALUE tail\n"));
      });

      it("prints the column names as a header line when asked", []()
      {
        TableFixture fixture(false, true);
        fixture.table.add_column("ddname", 9).add_column("id", 4).add_column("stepname");
        fixture.table.row().add("JESMSGLG").add(2).add("STEP1").emit();

        Expect(fixture.output()).ToBe(std::string("ddname    id   stepname\nJESMSGLG  2    STEP1\n"));
      });

      it("prints the header once, ahead of every row", []()
      {
        TableFixture fixture(false, true);
        fixture.table.add_column("name");
        fixture.table.row().add("ONE").emit();
        fixture.table.row().add("TWO").emit();

        Expect(fixture.output()).ToBe(std::string("name\nONE\nTWO\n"));
      });

      it("omits the header line when not asked for", []()
      {
        TableFixture fixture(false, false);
        fixture.table.add_column("name");
        fixture.table.row().add("ONE").emit();

        Expect(fixture.output()).ToBe(std::string("ONE\n"));
      });
    });

    describe("CSV output", []() -> void
    {
      it("joins the cells of a row with commas", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("dsname", 44);
        fixture.table.row().add("SYS1.PARMLIB").emit();

        Expect(fixture.output()).ToBe(std::string("SYS1.PARMLIB\n"));
      });

      it("gives every row the same field count", []()
      {
        // Regression test: job list-files reused one field vector across the
        // whole loop without clearing it, so the second record carried the
        // first record's fields ahead of its own.
        TableFixture fixture(true);
        fixture.table.add_column("ddname", 9)
            .add_column("dsname")
            .add_column("id", 4)
            .add_column("stepname")
            .add_column("procstep");
        fixture.table.row().add("A").add("B").add(1).add("C").add("D").emit();
        fixture.table.row().add("E").add("F").add(2).add("G").add("H").emit();

        Expect(fixture.output()).ToBe(std::string("A,B,1,C,D\nE,F,2,G,H\n"));
      });

      it("pads a short row out to the declared column count", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("member", 12).add_column("vers", 4).add_column("mod", 4);
        fixture.table.row().add("MEMBER1").emit();

        Expect(fixture.output()).ToBe(std::string("MEMBER1,,\n"));
      });

      it("trims padding the control blocks leave on a field", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("jobid").add_column("owner");
        fixture.table.row().add("JOB00042  ").add("  IBMUSER").emit();

        Expect(fixture.output()).ToBe(std::string("JOB00042,IBMUSER\n"));
      });

      it("prints the column names as a header record when asked", []()
      {
        TableFixture fixture(true, true);
        fixture.table.add_column("jobid").add_column("owner");
        fixture.table.row().add("JOB00042").add("IBMUSER").emit();

        Expect(fixture.output()).ToBe(std::string("jobid,owner\nJOB00042,IBMUSER\n"));
      });
    });

    describe("cell helpers", []() -> void
    {
      it("renders the unset sentinel as a blank cell", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("lrecl").add_column("blksize").add_column("primary");
        fixture.table.row().add_or_blank(-1).add_or_blank(0).add_or_blank(80).emit();

        Expect(fixture.output()).ToBe(std::string(",0,80\n"));
      });

      it("honours a caller-supplied sentinel", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("a").add_column("b");
        fixture.table.row().add_or_blank(0, 0).add_or_blank(-1, 0).emit();

        Expect(fixture.output()).ToBe(std::string(",-1\n"));
      });

      it("renders a flag with the command's own wording", []()
      {
        TableFixture fixture(true);
        fixture.table.add_column("migrated").add_column("sclm");
        fixture.table.row().add_flag(true, "YES", "NO").add_flag(false, "Y", "N").emit();

        Expect(fixture.output()).ToBe(std::string("YES,N\n"));
      });
    });

    describe("structured result", []() -> void
    {
      it("collects the emitted objects under the items key", []()
      {
        TableFixture fixture(false);
        fixture.table.add_column("name");

        const auto first = ast::obj();
        first->set("name", ast::str("ONE"));
        fixture.table.row().add("ONE").emit(first);

        const auto second = ast::obj();
        second->set("name", ast::str("TWO"));
        fixture.table.row().add("TWO").emit(second);

        fixture.table.finish();

        const ast::Node result = fixture.context().get_object();
        Expect(result != nullptr).ToBe(true);
        const ast::Node items = result->get("items");
        Expect(items != nullptr).ToBe(true);
        Expect(items->as_array().size()).ToBe(static_cast<size_t>(2));
        Expect(items->at(0)->get("name")->as_string()).ToBe(std::string("ONE"));
        Expect(result->get("returnedRows") == nullptr).ToBe(true);
      });

      it("reports the row count when asked", []()
      {
        TableFixture fixture(false);
        fixture.table.add_column("name");
        fixture.table.row().add("ONE").emit(ast::obj());
        fixture.table.finish("items", true);

        const ast::Node result = fixture.context().get_object();
        const ast::Node count = result->get("returnedRows");
        Expect(count != nullptr).ToBe(true);
        Expect(count->as_integer()).ToBe(static_cast<long long>(1));
      });

      it("omits a row that carries no object", []()
      {
        // job view-status renders one line but builds its own result object.
        TableFixture fixture(false);
        fixture.table.add_column("jobid");
        fixture.table.row().add("ONLY").emit();
        fixture.table.finish();

        const ast::Node items = fixture.context().get_object()->get("items");
        Expect(items->as_array().empty()).ToBe(true);
        Expect(fixture.output()).ToBe(std::string("ONLY\n"));
      });
    });
  });
}

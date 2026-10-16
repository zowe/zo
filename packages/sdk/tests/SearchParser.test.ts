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

import { describe, expect, it } from "vitest";
import { parseSearchOutput } from "../src/utils/tools/SearchParser";

describe("parseSearchOutput", () => {
    it("properly parses PDS member search", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.49    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.SAMPLIB(IEANTCOB)",
            "",
            "      *        ***************************************************************** 00050000",
            "      5        *  Descriptive Name: Name/Token Service COBOL Declares          * 00250000",
            "     18        *    IEANTCOB defines Name/Token Service constants and external * 00500000",
            "     75        *  End of Name/Token Service Declares                           * 02650000",
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.49    PAGE     2",
            "     SEARCH-FOR SUMMARY SECTION            SRCH DSN: SYS1.SAMPLIB(IEANTCOB)",
            "",
            "LINES-FOUND  LINES-PROC  DATASET-W/LNS  DATASET-WO/LNS  COMPARE-COLS  LONGEST-LINE",
            "        3           76            1              0           1:80           80",
            "",
            "PROCESS OPTIONS USED: ANYC LPSF",
            "",
            "THE FOLLOWING PROCESS STATEMENTS (USING COLUMNS 1:72) WERE PROCESSED:",
            "    SRCHFOR 'Name/Token Service'",
            "",
            "",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.dataset).toBe("SYS1.SAMPLIB(IEANTCOB)");
        expect(result.members).toHaveLength(1);
        expect(result.members[0].name).toBe("IEANTCOB");
        expect(result.members[0].matches.map((m) => m.lineNumber)).toEqual([5, 18, 75]);
        expect(result.summary.linesFound).toBe(3);
        expect(result.summary.membersWithLines).toBe(1);
    });

    it("should parse PDS search with multiple members", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: IBMUSER.JCL",
            "",
            "",
            " GDG                         --------- STRING(S) FOUND -------------------",
            "",
            "      1  //IEFBR14$ JOB (IZUACCT),'mainframer',REGION=0M                        JOB00730",
            "      *  //*",
            " IEFBR14                     --------- STRING(S) FOUND -------------------",
            "",
            "      4  //EXEC     EXEC PGM=IEFBR14",
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     3",
            "     SEARCH-FOR SUMMARY SECTION            SRCH DSN: IBMUSER.JCL",
            "",
            "LINES-FOUND  LINES-PROC  MEMBERS-W/LNS  MEMBERS-WO/LNS  COMPARE-COLS  LONGEST-LINE",
            "       10          937            8             37           1:80           80",
            "",
            "THE FOLLOWING PROCESS STATEMENTS (USING COLUMNS 1:72) WERE PROCESSED:",
            "    SRCHFOR 'ief'",
            "",
            "",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.members.map((m) => m.name)).toEqual(["GDG", "IEFBR14"]);
        expect(result.dataset).toBe("IBMUSER.JCL");
        expect(result.summary.membersWithLines).toEqual(8);
        expect(result.members[0].matches.map((m) => m.lineNumber)).toEqual([1]);
        expect(result.members[1].matches.map((m) => m.lineNumber)).toEqual([4]);
        expect(result.summary.linesFound).toBe(10);
    });

    it("should handle member with no matches at SOURCE SECTION transition and end of file", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.49    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.SAMPLIB(MEMBER1)",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.SAMPLIB(MEMBER2)",
            "",
        ].join("\n");

        const result = parseSearchOutput(output);
        expect(result.members).toHaveLength(2);
        expect(result.members[0].name).toBe("MEMBER1");
        expect(result.members[0].matches).toHaveLength(0);
        expect(result.members[1].name).toBe("MEMBER2");
        expect(result.members[1].matches).toHaveLength(0);
    });

    it("should handle member with matches at SOURCE SECTION transitions", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.49    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.SAMPLIB(MEMBER1)",
            "      5        *  Match in member 1",
            "      *  Some context in member 1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.SAMPLIB(MEMBER2)",
            "     10        *  Match in member 2",
            "      *  Some context in member 2",
        ].join("\n");

        const result = parseSearchOutput(output);
        expect(result.members).toHaveLength(2);
        expect(result.members[0].name).toBe("MEMBER1");
        expect(result.members[0].matches[0].afterContext).toContain("Some context in member 1");
        expect(result.members[1].name).toBe("MEMBER2");
        expect(result.members[1].matches[0].afterContext).toContain("Some context in member 2");
    });

    it("should parse a sequential (non-PDS) data set search with no member name", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: IBMUSER.SEQ",
            "",
            "      3  //IEFBR14$ JOB (IZUACCT),'mainframer',REGION=0M                        JOB00730",
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     2",
            "     SEARCH-FOR SUMMARY SECTION            SRCH DSN: IBMUSER.SEQ",
            "",
            "LINES-FOUND  LINES-PROC  DATASET-W/LNS  DATASET-WO/LNS  COMPARE-COLS  LONGEST-LINE",
            "        1           50            1              0           1:80           80",
            "",
            "THE FOLLOWING PROCESS STATEMENTS (USING COLUMNS 1:72) WERE PROCESSED:",
            "    SRCHFOR 'ief'",
            "",
            "",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.dataset).toBe("IBMUSER.SEQ");
        expect(result.members).toHaveLength(1);
        expect(result.members[0].name).toBeUndefined();
        expect(result.members[0].matches.map((m) => m.lineNumber)).toEqual([3]);
        expect(result.summary.linesFound).toBe(1);
    });

    it("should not create an empty section for a full-PDS scan's leading banner line", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: IBMUSER.JCL",
            "",
            "",
            " IEFBR14                     --------- STRING(S) FOUND -------------------",
            "",
            "      4  //EXEC     EXEC PGM=IEFBR14",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.members).toHaveLength(1);
        expect(result.members[0].name).toBe("IEFBR14");
    });

    it("should keep member name across SuperC page breaks and strip fused page headers", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/10/05  16.41    PAGE     5",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.MACLIB",
            "",
            " IHASDWA                     --------- STRING(S) FOUND -------------------",
            "",
            "        65  */*     AN ESTAE-TYPE RECOVERY ROUTINE IF REGISTER 0 DOES NOT    */",
            "      1121  SDWATEST EQU  2        ESTAE/I/X WAS GIVEN CONTROL               @L4A",
            "      1141  SDWAEADR DS   CL4      FRR OR ESTAE RECOVERY ROUTINE ADDRESS.    ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/10/05  16.41    PAGE     6",
            " LINE-#  SOURCE SECTION                    SRCH DSN: SYS1.MACLIB",
            "",
            "      1146  *                      ON ENTRY TO AN ESTAE: 0                   @G860P38",
            "      1190  *   FRR PERCOLATION AND ARE ZEROED ON RTM2 ESTAE PERCOLATION.",
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/10/05  16.41    PAGE     7",
            "     SEARCH-FOR SUMMARY SECTION            SRCH DSN: SYS1.MACLIB",
            "",
            "LINES-FOUND  LINES-PROC  MEMBERS-W/LNS  MEMBERS-WO/LNS  COMPARE-COLS  LONGEST-LINE",
            "         5          200            1              10           1:80           80",
            "",
            "THE FOLLOWING PROCESS STATEMENTS (USING COLUMNS 1:72) WERE PROCESSED:",
            "    SRCHFOR 'ESTAE'",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.members).toHaveLength(1);
        expect(result.members[0].name).toBe("IHASDWA");
        expect(result.members[0].matches.map((m) => m.lineNumber)).toEqual([65, 1121, 1141, 1146, 1190]);
        expect(result.members[0].matches[2].content).toBe("SDWAEADR DS   CL4      FRR OR ESTAE RECOVERY ROUTINE ADDRESS.");
        expect(result.header).toBe(
            "ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/10/05  16.41    PAGE     7",
        );
        expect(result.summary.linesFound).toBe(5);
        expect(result.summary.membersWithLines).toBe(1);
    });

    it("should merge multiple same-name member sections into a single entry", () => {
        const output = [
            "\f  ASMFSUPC    -     MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0  (2021/11/01)  2026/04/21   8.53    PAGE     1",
            " LINE-#  SOURCE SECTION                    SRCH DSN: IBMUSER.JCL",
            "",
            " FOO                         --------- STRING(S) FOUND -------------------",
            "        10  //STEP1    EXEC PGM=FOO",
            " BAR                         --------- STRING(S) FOUND -------------------",
            "        20  //STEP2    EXEC PGM=BAR",
            " FOO                         --------- STRING(S) FOUND -------------------",
            "        30  //STEP3    EXEC PGM=FOO2",
        ].join("\n");

        const result = parseSearchOutput(output);

        expect(result.members).toHaveLength(2);
        expect(result.members.map((m) => m.name)).toEqual(["FOO", "BAR"]);
        expect(result.members[0].matches.map((m) => m.lineNumber)).toEqual([10, 30]);
        expect(result.members[1].matches.map((m) => m.lineNumber)).toEqual([20]);
    });
});

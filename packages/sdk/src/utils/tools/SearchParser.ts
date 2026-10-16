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

export interface SearchMatch {
    lineNumber: number;
    content: string;
    beforeContext: string[];
    afterContext: string[];
}

export interface SearchMember {
    /** Absent when the search target is a sequential data set rather than a PDS member */
    name?: string;
    matches: SearchMatch[];
}

export interface SearchSummary {
    linesFound: number;
    linesProcessed: number;
    membersWithLines: number;
    membersWithoutLines: number;
    compareColumns: string;
    longestLine: number;
    processOptions: string; // See: https://www.ibm.com/docs/en/zos/3.1.0?topic=reference-process-options
    searchPattern: string;
}

export interface SearchResult {
    dataset: string;
    header: string;
    members: SearchMember[];
    summary: SearchSummary;
}

export function parseSearchOutput(output: string): SearchResult {
    const lines = output.split("\n");

    let dataset = "";
    let header = "";
    const members: SearchMember[] = [];
    let currentMember: SearchMember | null = null;

    let pendingContext: string[] = [];
    let linesFound = 0;
    let linesProcessed = 0;
    let membersWithLines = 0;
    let membersWithoutLines = 0;
    let compareColumns = "";
    let longestLine = 0;
    let processOptions = "";
    let searchPattern = "";

    // Closes out the section in progress
    const closeCurrentMember = (): void => {
        if (currentMember && (currentMember.name != null || currentMember.matches.length > 0)) {
            const lastMatch = currentMember.matches.at(-1);
            if (lastMatch) lastMatch.afterContext.push(...pendingContext);
            if (currentMember.name != null) {
                const existing = members.find((m) => m.name === currentMember!.name);
                if (existing) {
                    existing.matches.push(...currentMember.matches);
                } else {
                    members.push(currentMember);
                }
            } else {
                members.push(currentMember);
            }
        }
        pendingContext = [];
        currentMember = null;
    };

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];

        // NOTE(Kelosky): ASMFSUPC - MVS FILE/LINE/WORD/BYTE/SFOR COMPARE UTILITY- V1R6M0 (2021/11/01) 2026/02/20 9.05
        if (/ASMFSUPC/.test(line) && /COMPARE UTILITY/.test(line)) {
            const headerMatch = line.match(/(?:^|\f|\s{2,})(ASMFSUPC\s+-.*COMPARE UTILITY.*)$/);
            header = (headerMatch ? headerMatch[1] : line).trim();
        }

        // Parse "SRCH DSN:"
        const dsMatch = line.match(/SRCH DSN:\s+(\S+)/);
        if (dsMatch) {
            dataset = dsMatch[1];

            if (line.includes("SOURCE SECTION")) {
                // Parse member name from dataset name if present; sequential data sets have no member
                const memParse = dsMatch[1].match(/^([^()]+)\(([^)]+)\)\s*$/);
                const memberName = memParse?.[2];

                if (memberName) {
                    if (currentMember && currentMember.name === memberName) {
                        continue;
                    }
                    closeCurrentMember();
                    currentMember = {
                        name: memberName,
                        matches: [],
                    };
                    continue;
                } else {
                    if (currentMember) {
                        // Across a SuperC page break within a member (or sequential dataset),
                        // SuperC outputs "LINE-#  SOURCE SECTION  SRCH DSN: <DSN>" without
                        // repeating the member name. Keep accumulating into the existing member.
                        continue;
                    }
                    currentMember = {
                        name: undefined,
                        matches: [],
                    };
                    continue;
                }
            }
        }

        // Parse member header: " <member name>                    --------- STRING(S) FOUND -------------------"
        const memberMatch = line.match(/^\s+(\S+)\s+[-]+\s+STRING\(S\) FOUND\s+[-]+/);
        if (memberMatch) {
            closeCurrentMember();
            currentMember = {
                name: memberMatch[1],
                matches: [],
            };
            continue;
        }

        // parse summary
        const summaryMatch = line.match(/^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+:\d+)\s+(\d+)\s*$/);
        if (summaryMatch) {
            closeCurrentMember();
            linesFound = parseInt(summaryMatch[1], 10);
            linesProcessed = parseInt(summaryMatch[2], 10);
            membersWithLines = parseInt(summaryMatch[3], 10);
            membersWithoutLines = parseInt(summaryMatch[4], 10);
            compareColumns = summaryMatch[5];
            longestLine = parseInt(summaryMatch[6], 10);
            continue;
        }

        // parse match e.g. "      1  //IEFBR14$ JOB (IZUACCT),'Some User',REGION=0M"
        // parse context e.g. "      *  //*"
        if (currentMember) {
            const matchLine = line.match(/^\s+(\d+)\s{2}(.+)$/);
            if (matchLine) {
                const lastMatch = currentMember.matches.at(-1);
                const content = matchLine[2].replace(/(?:\s*\f.*|\s+ASMFSUPC\s+-.*)$/, "").trimEnd();
                currentMember.matches.push({
                    lineNumber: parseInt(matchLine[1], 10),
                    content,
                    beforeContext: lastMatch ? [] : pendingContext,
                    afterContext: [],
                });
                if (lastMatch) {
                    lastMatch.afterContext.push(...pendingContext);
                }
                pendingContext = [];
                continue;
            }
            const contextLine = line.match(/^\s+\*\s{2}(.+)$/);
            if (contextLine) {
                const contextContent = contextLine[1].replace(/(?:\s*\f.*|\s+ASMFSUPC\s+-.*)$/, "").trimEnd();
                pendingContext.push(contextContent);
            }
        }

        // parse options
        const optionsMatch = line.match(/PROCESS OPTIONS USED:\s+(.+)/);
        if (optionsMatch) {
            processOptions = optionsMatch[1].trim();
        }

        // parse search pattern, e.g.: SRCHFOR 'IEF'
        const patternMatch = line.match(/SRCHFOR\s+'([^']+)'/);
        if (patternMatch) {
            searchPattern = patternMatch[1];
        }
    }

    closeCurrentMember();

    return {
        dataset,
        header,
        members,
        summary: {
            linesFound,
            linesProcessed,
            membersWithLines,
            membersWithoutLines,
            compareColumns,
            longestLine,
            processOptions,
            searchPattern,
        },
    };
}

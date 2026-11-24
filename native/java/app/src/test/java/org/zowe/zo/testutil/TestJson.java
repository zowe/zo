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

package org.zowe.zo.testutil;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

public final class TestJson {

    private static final ObjectMapper JSON = new ObjectMapper();

    public static final String CORRELATOR = "J0000001SY1.....ABCDEF";

    private TestJson() {
    }

    public static JsonNode json(String text) {
        try {
            return JSON.readTree(text);
        } catch (Exception e) {
            throw new IllegalArgumentException(e);
        }
    }

    /** A job as returned by zo's getJobStatus / listJobs. */
    public static JsonNode job(String id, String name) {
        return json("{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"subsystem\":\"JES2\",\"owner\":\"IBMUSER\","
                + "\"status\":\"OUTPUT\",\"type\":\"JOB\",\"class\":\"A\",\"retcode\":\"CC 0000\",\"correlator\":\"" + CORRELATOR + "\","
                + "\"phase\":20,\"phaseName\":\"Job is on the hard copy queue\",\"execSystem\":\"SY1\",\"execMember\":\"JES2\","
                + "\"execSubmitted\":\"2026-01-01T10:00:00.000Z\",\"execStarted\":\"2026-01-01T10:00:01.000Z\","
                + "\"execEnded\":\"2026-01-01T10:00:02.000Z\"}");
    }
}

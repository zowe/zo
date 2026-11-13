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

package org.zowe.zo.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class TsoServiceTest {

    private final ObjectMapper json = new ObjectMapper();
    private final ZoRpcClient rpc = mock(ZoRpcClient.class);

    private static void as(String user) {
        SecurityContextHolder.getContext().setAuthentication(new UsernamePasswordAuthenticationToken(user, null, List.of()));
    }

    @AfterEach
    void clear() {
        SecurityContextHolder.clearContext();
    }

    private TsoService service(Clock clock) {
        return new TsoService(rpc, clock, 2, Duration.ofMinutes(10), 1000);
    }

    @Test
    void issueSendsCommandTextAndReturnsOutput() throws Exception {
        when(rpc.call(eq("tsoCommand"), eq(Map.of("commandText", "TIME")))).thenReturn(json.readTree("{\"data\":\"IKJ56650I TIME-10:00\"}"));
        var result = service(Clock.systemUTC()).issue("TIME");
        assertEquals(0, result.rc());
        assertEquals("IKJ56650I TIME-10:00", result.output());
    }

    @Test
    void nonZeroTsoReturnCodeIsAResultNotAnError() throws Exception {
        when(rpc.call(eq("tsoCommand"), eq(Map.of("commandText", "LISTCAT ENT('X')"))))
                .thenThrow(new ZoRpcException("tsoCommand", 4, "Error running command, rc '4'",
                        json.readTree("\"  Details: \\nIDC3012I ENTRY X NOT FOUND\"")));
        var result = service(Clock.systemUTC()).issue("LISTCAT ENT('X')");
        assertEquals(4, result.rc());
        assertTrue(result.output().startsWith("IDC3012I"));
    }

    @Test
    void rejectsInvalidCommands() {
        var tso = service(Clock.systemUTC());
        assertThrows(IllegalArgumentException.class, () -> tso.issue(""));
        assertThrows(IllegalArgumentException.class, () -> tso.issue("TIME\nLOGOFF"));
        assertThrows(IllegalArgumentException.class, () -> tso.issue("X".repeat(TsoService.MAX_COMMAND_LENGTH + 1)));
    }

    @Test
    void sessionBuffersOutputUntilReceivedAndIsOwnerScoped() throws Exception {
        when(rpc.call(eq("tsoCommand"), eq(Map.of("commandText", "TIME")))).thenReturn(json.readTree("{\"data\":\"T\"}"));
        var tso = service(Clock.systemUTC());
        as("user1");
        var session = tso.start();
        assertEquals("USER1", session.owner());
        tso.send(session.sessionId(), "TIME");
        tso.send(session.sessionId(), "TIME");
        assertEquals(2, tso.receive(session.sessionId()).size());
        assertEquals(0, tso.receive(session.sessionId()).size());

        as("user2");
        var e = assertThrows(ZoRpcException.class, () -> tso.receive(session.sessionId()));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());

        as("user1");
        tso.end(session.sessionId());
        assertThrows(ZoRpcException.class, () -> tso.receive(session.sessionId()));
    }

    @Test
    void sessionLimitAndIdleExpiry() throws Exception {
        var now = new java.util.concurrent.atomic.AtomicReference<>(Instant.parse("2026-11-13T00:00:00Z"));
        Clock clock = new Clock() {
            @Override public ZoneOffset getZone() { return ZoneOffset.UTC; }
            @Override public Clock withZone(java.time.ZoneId zone) { return this; }
            @Override public Instant instant() { return now.get(); }
        };
        var tso = service(clock);
        as("user1");
        var first = tso.start();
        tso.start();
        var limit = assertThrows(ZoRpcException.class, tso::start);
        assertEquals(ZoRpcException.CONFLICT, limit.getCode());

        now.set(now.get().plus(Duration.ofMinutes(11)));
        assertThrows(ZoRpcException.class, () -> tso.receive(first.sessionId()));
        tso.start();
    }

    @Test
    void sessionsRequireAnAuthenticatedUser() {
        assertThrows(ZoRpcException.class, () -> service(Clock.systemUTC()).start());
    }
}

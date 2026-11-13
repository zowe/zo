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

import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.security.core.Authentication;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

/**
 * TSO commands through zo's one-shot tsocmd, plus an emulated session API on top of it.
 *
 * Every command runs in a fresh TSO environment under the caller's identity, so nothing carries
 * over between commands of a "session" (no allocations, prefix, REXX state or prompting). A session
 * here is only an owner-scoped output buffer. See native/java/TSO-LIMITATIONS.md.
 */
@Service
public class TsoService {

    public static final int MAX_COMMAND_LENGTH = 1024;

    public record CommandResult(String command, int rc, String output) {
    }

    public record SessionInfo(String sessionId, String owner, Instant created, Instant expires) {
    }

    private static final class Session {
        final String id;
        final String owner;
        final Instant created;
        volatile Instant lastUsed;
        final List<CommandResult> pending = new ArrayList<>();
        int pendingBytes;

        Session(String id, String owner, Instant now) {
            this.id = id;
            this.owner = owner;
            this.created = now;
            this.lastUsed = now;
        }
    }

    private final ZoRpcClient rpc;
    private final Clock clock;
    private final int maxSessionsPerUser;
    private final Duration idleTimeout;
    private final int maxBufferedBytes;
    private final Map<String, Session> sessions = new ConcurrentHashMap<>();

    @org.springframework.beans.factory.annotation.Autowired
    public TsoService(ZoRpcClient rpc,
                      @Value("${zo.tso.max-sessions-per-user:5}") int maxSessionsPerUser,
                      @Value("${zo.tso.idle-timeout-seconds:900}") long idleTimeoutSeconds,
                      @Value("${zo.tso.max-buffered-bytes:1048576}") int maxBufferedBytes) {
        this(rpc, Clock.systemUTC(), maxSessionsPerUser, Duration.ofSeconds(idleTimeoutSeconds), maxBufferedBytes);
    }

    TsoService(ZoRpcClient rpc, Clock clock, int maxSessionsPerUser, Duration idleTimeout, int maxBufferedBytes) {
        this.rpc = rpc;
        this.clock = clock;
        this.maxSessionsPerUser = maxSessionsPerUser;
        this.idleTimeout = idleTimeout;
        this.maxBufferedBytes = maxBufferedBytes;
    }

    /** Runs one TSO command. A non-zero TSO return code is a result, not an error. */
    public CommandResult issue(String command) throws ZoRpcException {
        String cmd = validated(command);
        try {
            JsonNode result = rpc.call("tsoCommand", Map.of("commandText", cmd));
            return new CommandResult(cmd, 0, text(result.path("data")));
        } catch (ZoRpcException e) {
            if (e.getCode() > 0) {
                return new CommandResult(cmd, e.getCode(), stripDetailsPrefix(e.getDetails()));
            }
            throw e;
        }
    }

    public SessionInfo start() throws ZoRpcException {
        String owner = currentUser();
        Instant now = clock.instant();
        expireIdle(now);
        long owned = sessions.values().stream().filter(s -> s.owner.equals(owner)).count();
        if (owned >= maxSessionsPerUser) {
            throw new ZoRpcException("tsoSession", ZoRpcException.CONFLICT,
                    "Too many TSO sessions for this user (limit " + maxSessionsPerUser + "); end one first", null);
        }
        Session session = new Session(UUID.randomUUID().toString(), owner, now);
        sessions.put(session.id, session);
        return info(session);
    }

    /** Runs a command in the session, buffers its output for {@link #receive}, and returns it. */
    public CommandResult send(String sessionId, String command) throws ZoRpcException {
        Session session = owned(sessionId);
        CommandResult result = issue(command);
        synchronized (session) {
            int size = result.output() == null ? 0 : result.output().length();
            if (session.pendingBytes + size > maxBufferedBytes) {
                throw new ZoRpcException("tsoSession", ZoRpcException.CONFLICT,
                        "Session output buffer is full; receive pending output first", null);
            }
            session.pending.add(result);
            session.pendingBytes += size;
            session.lastUsed = clock.instant();
        }
        return result;
    }

    /** Returns and clears the output buffered since the last receive. */
    public List<CommandResult> receive(String sessionId) throws ZoRpcException {
        Session session = owned(sessionId);
        synchronized (session) {
            List<CommandResult> out = List.copyOf(session.pending);
            session.pending.clear();
            session.pendingBytes = 0;
            session.lastUsed = clock.instant();
            return out;
        }
    }

    public void end(String sessionId) throws ZoRpcException {
        owned(sessionId);
        sessions.remove(sessionId);
    }

    public SessionInfo describe(String sessionId) throws ZoRpcException {
        return info(owned(sessionId));
    }

    private Session owned(String sessionId) throws ZoRpcException {
        expireIdle(clock.instant());
        Session session = sessionId == null ? null : sessions.get(sessionId);
        // Another user's session is reported as missing so session IDs cannot be probed
        if (session == null || !session.owner.equals(currentUser())) {
            throw new ZoRpcException("tsoSession", ZoRpcException.NOT_FOUND, "TSO session not found", null);
        }
        return session;
    }

    private void expireIdle(Instant now) {
        sessions.values().removeIf(s -> s.lastUsed.plus(idleTimeout).isBefore(now));
    }

    private SessionInfo info(Session s) {
        return new SessionInfo(s.id, s.owner, s.created, s.lastUsed.plus(idleTimeout));
    }

    private static String currentUser() throws ZoRpcException {
        Authentication auth = SecurityContextHolder.getContext().getAuthentication();
        if (auth == null || auth.getName() == null || auth.getName().isBlank()) {
            throw new ZoRpcException("tsoSession", ZoRpcException.FORBIDDEN, "Authentication is required", null);
        }
        return auth.getName().toUpperCase(Locale.ROOT);
    }

    private static String validated(String command) {
        if (command == null || command.isBlank() || command.length() > MAX_COMMAND_LENGTH
                || command.chars().anyMatch(c -> c < 0x20 || c > 0xFF)) {
            throw new IllegalArgumentException("'command' is required: printable Latin-1, at most "
                    + MAX_COMMAND_LENGTH + " characters");
        }
        return command;
    }

    private static String text(JsonNode node) {
        return node == null || node.isMissingNode() || node.isNull() ? "" : node.asText();
    }

    private static String stripDetailsPrefix(String details) {
        if (details == null) return "";
        return details.replaceFirst("^\\s*Details:\\s*\\n?", "");
    }
}

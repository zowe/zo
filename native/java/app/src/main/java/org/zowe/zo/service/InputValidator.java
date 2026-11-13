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

import org.zowe.zo.ffm.ZoRpcException;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/** Allow-list validation for every client-supplied value that reaches zo. */
public final class InputValidator {

    private static final Pattern JOB_NAME = Pattern.compile("[A-Z0-9@#$]{1,8}");
    private static final Pattern JOB_NAME_PATTERN = Pattern.compile("[A-Z0-9@#$*]{1,8}");
    private static final Pattern JOB_ID = Pattern.compile("(?:JOB|STC|TSU)\\d{5}|[JST]\\d{7}");
    private static final Pattern CORRELATOR = Pattern.compile("[A-Za-z0-9@#$._:~-]{9,64}");
    private static final Pattern STATUS = Pattern.compile("ACTIVE|OUTPUT|INPUT|\\*");
    private static final Pattern DSN = Pattern.compile("[A-Z0-9@#$-]{1,8}(?:\\.[A-Z0-9@#$-]{1,8}){0,21}(?:\\([A-Z0-9@#$]{1,8}\\))?");
    private static final Pattern OCTAL_MODE = Pattern.compile("[0-7]{3,4}");
    private static final Pattern SYMBOLIC_MODE = Pattern.compile("[r-][w-][x-][r-][w-][x-][r-][w-][x-]");
    private static final Pattern OWNER_NAME = Pattern.compile("[A-Za-z0-9@#$_.-]{1,8}|\\d{1,10}");
    private static final Pattern CODESET = Pattern.compile("[A-Za-z0-9][A-Za-z0-9._:-]{0,14}");
    private static final int MAX_PATH = 1023;

    private InputValidator() {
    }

    public static String jobName(String value) {
        return match(value, JOB_NAME, "job name", true);
    }

    public static String jobNamePattern(String value) {
        return match(value, JOB_NAME_PATTERN, "job name pattern", true);
    }

    public static String jobId(String value) {
        return match(value, JOB_ID, "job ID", true);
    }

    public static String correlator(String value) {
        return match(value, CORRELATOR, "job correlator", false);
    }

    /** A job ID, or a correlator (always longer than 8 characters, which zo uses to tell them apart). */
    public static String jobRef(String value) {
        if (value != null && value.length() > 8) return correlator(value);
        return jobId(value);
    }

    public static String jobStatus(String value) {
        return match(value, STATUS, "status", true);
    }

    public static String dsn(String value) {
        return match(value, DSN, "data set name", true);
    }

    public static String codeset(String value) {
        return match(value, CODESET, "codeset", false);
    }

    public static String owner(String value) {
        return match(value, OWNER_NAME, "owner", false);
    }

    public static String group(String value) {
        return match(value, OWNER_NAME, "group", false);
    }

    /** Validates an octal or symbolic mode and returns it in octal form. */
    public static String mode(String value) {
        if (value == null) throw new IllegalArgumentException("mode is required");
        if (OCTAL_MODE.matcher(value).matches()) return value;
        if (SYMBOLIC_MODE.matcher(value).matches()) {
            StringBuilder octal = new StringBuilder();
            for (int i = 0; i < 9; i += 3) {
                int digit = (value.charAt(i) == 'r' ? 4 : 0) + (value.charAt(i + 1) == 'w' ? 2 : 0) + (value.charAt(i + 2) == 'x' ? 1 : 0);
                octal.append(digit);
            }
            return octal.toString();
        }
        throw new IllegalArgumentException("Invalid mode '" + printable(value) + "': use octal (e.g. 755) or rwxr-xr-x form");
    }

    /** Requires an absolute path without NUL or control characters, and normalizes it lexically (symlinks are never resolved). */
    public static String path(String value) {
        if (value == null || value.isEmpty()) throw new IllegalArgumentException("path is required");
        if (value.length() > MAX_PATH) throw new IllegalArgumentException("path is too long");
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c < 0x20 || c == 0x7f) throw new IllegalArgumentException("path contains control characters");
        }
        if (value.charAt(0) != '/') throw new IllegalArgumentException("path must be absolute");
        List<String> segments = new ArrayList<>();
        for (String segment : value.split("/")) {
            if (segment.isEmpty() || segment.equals(".")) continue;
            if (segment.equals("..")) {
                if (!segments.isEmpty()) segments.removeLast();
                continue;
            }
            segments.add(segment);
        }
        return "/" + String.join("/", segments);
    }

    /** Parses an optional integer parameter, turning malformed input into a 400. */
    public static Integer integer(String label, String value) {
        if (value == null || value.isBlank()) return null;
        try {
            return Integer.valueOf(value.trim());
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Invalid " + label + ": '" + printable(value) + "' is not an integer");
        }
    }

    public static ZoRpcException notImplemented(String message) {
        return new ZoRpcException("", ZoRpcException.METHOD_NOT_FOUND, message, null);
    }

    public static ZoRpcException notFound(String method, String message) {
        return new ZoRpcException(method, ZoRpcException.NOT_FOUND, message, null);
    }

    private static String match(String value, Pattern pattern, String label, boolean upper) {
        if (value == null || value.isEmpty()) throw new IllegalArgumentException(label + " is required");
        String candidate = upper ? value.toUpperCase(Locale.ROOT) : value;
        if (!pattern.matcher(candidate).matches()) throw new IllegalArgumentException("Invalid " + label + ": '" + printable(value) + "'");
        return candidate;
    }

    private static String printable(String value) {
        String cut = value.length() > 40 ? value.substring(0, 40) : value;
        return cut.replaceAll("[^\\x20-\\x7e]", "?");
    }
}

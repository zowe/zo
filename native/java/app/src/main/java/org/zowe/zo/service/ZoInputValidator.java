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

import java.time.LocalDate;
import java.time.LocalTime;
import java.time.format.DateTimeParseException;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

/** Input rules for the zo extension endpoints. Messages never echo secret values. */
public final class ZoInputValidator {

    public static final int MAX_OWNER_LENGTH = 8;
    public static final int MAX_RING_LENGTH = 236;
    public static final int MAX_LABEL_LENGTH = 32;
    public static final int MAX_PATH_LENGTH = 1023;
    public static final int MIN_EXPORT_PASSWORD = 8;
    public static final int MAX_EXPORT_PASSWORD = 128;
    public static final int MAX_IMPORT_PASSWORD = 256;
    public static final int MAX_SECONDS_AGO = 86400;
    public static final int MAX_SYSLOG_LINES = 10000;
    public static final int MAX_CERT_ENTRIES = 100000;
    public static final String VIRTUAL_RING = "*";

    private static final Pattern OWNER = Pattern.compile("[A-Za-z0-9@#$]{1,8}");
    private static final Pattern DATE = Pattern.compile("\\d{4}-\\d{2}-\\d{2}");
    private static final Pattern TIME = Pattern.compile("\\d{2}:\\d{2}:\\d{2}");
    private static final Pattern DSN = Pattern.compile(
            "[A-Z@#$][A-Z0-9@#$-]{0,7}(?:\\.[A-Z@#$][A-Z0-9@#$-]{0,7})*(?:\\([A-Z@#$][A-Z0-9@#$]{0,7}\\))?");
    private static final String LABEL_FORBIDDEN = "/;%\\";
    private static final String RING_FORBIDDEN = "'\"()/;%\\";
    private static final Set<String> USAGES = Set.of("PERSONAL", "CERTAUTH", "OTHER");
    private static final Set<String> WRITE_USAGES = Set.of("PERSONAL", "CERTAUTH");
    private static final Set<String> STATUSES = Set.of("TRUST", "HIGHTRUST", "NOTRUST");

    private ZoInputValidator() {
    }

    public static String owner(String value) {
        if (value == null || !OWNER.matcher(value).matches()) {
            throw new IllegalArgumentException("owner must be 1-" + MAX_OWNER_LENGTH + " characters (letters, digits, @, #, $)");
        }
        return value;
    }

    public static String ring(String value, boolean allowVirtual) {
        if (value == null || value.isEmpty() || value.length() > MAX_RING_LENGTH) {
            throw new IllegalArgumentException("keyring must be 1-" + MAX_RING_LENGTH + " characters");
        }
        if (VIRTUAL_RING.equals(value)) {
            if (!allowVirtual) {
                throw new IllegalArgumentException("'*' is the virtual key ring and is not valid for this operation");
            }
            return value;
        }
        if (!isPrintable(value) || containsAny(value, RING_FORBIDDEN) || value.isBlank()) {
            throw new IllegalArgumentException("keyring contains invalid characters");
        }
        return value;
    }

    public static String label(String value, String field) {
        if (value == null || value.isBlank() || value.length() > MAX_LABEL_LENGTH) {
            throw new IllegalArgumentException(field + " must be 1-" + MAX_LABEL_LENGTH + " characters");
        }
        if (!isPrintable(value) || containsAny(value, LABEL_FORBIDDEN)) {
            throw new IllegalArgumentException(field + " contains invalid characters");
        }
        return value;
    }

    public static String optionalLabel(String value) {
        return value == null ? null : label(value, "label");
    }

    public static String usage(String value, boolean readFilter) {
        String upper = value == null ? "" : value.toUpperCase(Locale.ROOT);
        if (!(readFilter ? USAGES : WRITE_USAGES).contains(upper)) {
            throw new IllegalArgumentException("usage must be one of " + (readFilter ? "PERSONAL, CERTAUTH, OTHER" : "PERSONAL, CERTAUTH"));
        }
        return upper;
    }

    public static String trustStatus(String value) {
        String upper = value == null ? "" : value.toUpperCase(Locale.ROOT);
        if (!STATUSES.contains(upper)) {
            throw new IllegalArgumentException("status must be one of TRUST, HIGHTRUST, NOTRUST");
        }
        return upper;
    }

    public static String exportFormat(String value) {
        String lower = value == null ? "pem" : value.toLowerCase(Locale.ROOT);
        if (!lower.equals("pem") && !lower.equals("p12")) {
            throw new IllegalArgumentException("format must be pem or p12");
        }
        return lower;
    }

    public static String dataSet(String value) {
        String upper = value == null ? "" : value.toUpperCase(Locale.ROOT);
        int member = upper.indexOf('(');
        int nameLength = member < 0 ? upper.length() : member;
        if (nameLength > 44 || !DSN.matcher(upper).matches()) {
            throw new IllegalArgumentException("dsn is not a valid data set name (optionally with a member)");
        }
        return upper;
    }

    public static String path(String value) {
        if (value == null || value.isEmpty() || value.length() > MAX_PATH_LENGTH || value.charAt(0) != '/') {
            throw new IllegalArgumentException("file must be an absolute path of at most " + MAX_PATH_LENGTH + " characters");
        }
        for (int i = 0; i < value.length(); i++) {
            if (Character.isISOControl(value.charAt(i))) {
                throw new IllegalArgumentException("file contains control characters");
            }
        }
        return value;
    }

    public static void fileOrDsn(String file, String dsn, boolean required) {
        if (file != null && dsn != null) {
            throw new IllegalArgumentException("specify file or dsn, not both");
        }
        if (required && file == null && dsn == null) {
            throw new IllegalArgumentException("file or dsn is required");
        }
    }

    public static String exportPassword(String value) {
        if (value == null || value.length() < MIN_EXPORT_PASSWORD || value.length() > MAX_EXPORT_PASSWORD || !isPrintable(value)) {
            throw new IllegalArgumentException("password must be " + MIN_EXPORT_PASSWORD + "-" + MAX_EXPORT_PASSWORD + " printable ASCII characters");
        }
        return value;
    }

    public static String importPassword(String value) {
        if (value == null || value.isEmpty() || value.length() > MAX_IMPORT_PASSWORD || value.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("password is required (at most " + MAX_IMPORT_PASSWORD + " characters)");
        }
        return value;
    }

    public static String syslogDate(String value) {
        if (value == null) return null;
        try {
            if (DATE.matcher(value).matches()) {
                LocalDate.parse(value);
                return value;
            }
        } catch (DateTimeParseException ignored) {
            // reported below
        }
        throw new IllegalArgumentException("date must be a valid yyyy-mm-dd date");
    }

    public static String syslogTime(String value) {
        if (value == null) return null;
        try {
            if (TIME.matcher(value).matches()) {
                LocalTime.parse(value);
                return value;
            }
        } catch (DateTimeParseException ignored) {
            // reported below
        }
        throw new IllegalArgumentException("time must be a valid hh:mm:ss time");
    }

    public static Integer bounded(Integer value, String field, int min, int max) {
        if (value != null && (value < min || value > max)) {
            throw new IllegalArgumentException(field + " must be between " + min + " and " + max);
        }
        return value;
    }

    private static boolean isPrintable(String value) {
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c < 0x20 || c > 0x7E) return false;
        }
        return true;
    }

    private static boolean containsAny(String value, String chars) {
        for (int i = 0; i < chars.length(); i++) {
            if (value.indexOf(chars.charAt(i)) >= 0) return true;
        }
        return false;
    }
}

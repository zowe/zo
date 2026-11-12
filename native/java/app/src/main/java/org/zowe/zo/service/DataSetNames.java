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

import java.util.Locale;
import java.util.regex.Pattern;

/** Validation and normalization of user-supplied data set identifiers. */
public final class DataSetNames {

    public static final int MAX_DSN_LENGTH = 44;

    private static final Pattern QUALIFIER = Pattern.compile("[A-Z@#$][A-Z0-9@#$-]{0,7}");
    private static final Pattern PATTERN_CHARS = Pattern.compile("[A-Z0-9@#$*%.-]{1,44}");
    private static final Pattern MEMBER = Pattern.compile("[A-Z@#$][A-Z0-9@#$]{0,7}");
    private static final Pattern MEMBER_PATTERN = Pattern.compile("[A-Z0-9@#$*%?]{1,8}");
    private static final Pattern VOLSER = Pattern.compile("[A-Z0-9]{1,6}");
    private static final Pattern CLASS_NAME = Pattern.compile("[A-Z0-9@#$]{1,8}");
    private static final Pattern CODEPAGE = Pattern.compile("[A-Za-z0-9_.:-]{1,32}");

    private DataSetNames() {
    }

    public static String noControl(String label, String value) {
        if (value == null) {
            throw new IllegalArgumentException(label + " is required");
        }
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0)) {
                throw new IllegalArgumentException(label + " contains control characters");
            }
        }
        return value;
    }

    private static String upper(String label, String value) {
        noControl(label, value);
        if (value.isEmpty()) {
            throw new IllegalArgumentException(label + " must not be empty");
        }
        return value.toUpperCase(Locale.ROOT);
    }

    public static String dsn(String raw) {
        String name = upper("Data set name", raw);
        if (name.length() > MAX_DSN_LENGTH) {
            throw new IllegalArgumentException("Data set name exceeds 44 characters");
        }
        for (String qualifier : name.split("\\.", -1)) {
            if (!QUALIFIER.matcher(qualifier).matches()) {
                throw new IllegalArgumentException("Invalid data set name qualifier '" + qualifier + "'");
            }
        }
        return name;
    }

    public static String member(String raw) {
        String name = upper("Member name", raw);
        if (!MEMBER.matcher(name).matches()) {
            throw new IllegalArgumentException("Invalid member name '" + name + "'");
        }
        return name;
    }

    /** A dslevel filter: a data set name that may also contain '*' and '%'. */
    public static String dslevel(String raw) {
        String pattern = upper("dslevel", raw);
        if (!PATTERN_CHARS.matcher(pattern).matches() || pattern.startsWith(".") || pattern.endsWith(".")
                || pattern.contains("..")) {
            throw new IllegalArgumentException("Invalid dslevel pattern '" + pattern + "'");
        }
        return pattern;
    }

    /** A member filter. z/OSMF wildcard '%' (one character) is translated to zo's '?'. */
    public static String memberPattern(String raw) {
        String pattern = upper("Member pattern", raw);
        if (!MEMBER_PATTERN.matcher(pattern).matches()) {
            throw new IllegalArgumentException("Invalid member pattern '" + pattern + "'");
        }
        return pattern.replace('%', '?');
    }

    public static String volser(String raw) {
        String volser = upper("volser", raw);
        if (!VOLSER.matcher(volser).matches()) {
            throw new IllegalArgumentException("volser must be 1-6 alphanumeric characters");
        }
        return volser;
    }

    public static String className(String label, String raw) {
        String name = upper(label, raw);
        if (!CLASS_NAME.matcher(name).matches()) {
            throw new IllegalArgumentException("Invalid " + label + " '" + name + "'");
        }
        return name;
    }

    public static String codepage(String raw) {
        noControl("Encoding", raw);
        if (!CODEPAGE.matcher(raw).matches()) {
            throw new IllegalArgumentException("Invalid encoding '" + raw + "'");
        }
        return raw;
    }
}

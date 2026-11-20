/*
 * This program and the accompanying materials are made available and may be used, at your option, under either:
 * * Eclipse Public License v2.0, available at https://www.eclipse.org/legal/epl-v20.html, OR
 * * Apache License, version 2.0, available at http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
 *
 * Copyright Contributors to the Zowe Project.
 */
package org.zowe.zo.zos.security.platform;

import lombok.extern.slf4j.Slf4j;

import javax.security.auth.Subject;
import java.lang.reflect.InvocationTargetException;
import java.security.Principal;
import java.util.Arrays;
import java.util.concurrent.Callable;

/**
 * Counterpart to https://www.ibm.com/docs/en/semeru-runtime-ce-z/25.0.0?topic=jaas-os390threadsubject for non-z/os compilation
 */
@Slf4j
public class OS390ThreadSubject{
    private OS390ThreadSubject() {
        /* This utility class should not be instantiated */
    }

    public static void callAs(String username, Callable<Void> action) {

        try {
            var subject = new Subject();
            subject.getPrincipals().add((Principal) Class.forName("com.ibm.security.auth.UsernamePrincipal").getConstructor(String.class).newInstance(username));
            Class.forName("com.ibm.security.auth.OS390ThreadSubject").getMethod("callAs", Subject.class, Callable.class)
                    .invoke(null, subject, action);
        } catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException | NoSuchMethodException
                 | SecurityException | ClassNotFoundException | InstantiationException e) {
            throw new SafPlatformError("Native z/OS Security Error: " + e.getCause().getMessage(), e);
        }
    }
}

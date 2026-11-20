/*
 * This program and the accompanying materials are made available and may be used, at your option, under either:
 * * Eclipse Public License v2.0, available at https://www.eclipse.org/legal/epl-v20.html, OR
 * * Apache License, version 2.0, available at http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
 *
 * Copyright Contributors to the Zowe Project.
 */
package org.zowe.zo.zos.security.service;

import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.InitializingBean;
import org.springframework.context.annotation.Profile;
import org.springframework.stereotype.Service;
import org.zowe.zo.zos.security.platform.MockPlatformAccessControl;

/**
 * Implements security functions that provide dummy behavior outside of z/OS.
 */
@Profile("!zos")
@Service("platformSecurityService")
@Slf4j
public class DummyPlatformSecurityService extends AccessControlService implements PlatformSecurityService, InitializingBean {
    static final String INVALID_VALUE = "INVALID";

    private static final SecurityRequestFailed SECURITY_REQUEST_FAILED = new SecurityRequestFailed(null, 0, 1);

    private static final String SERVER_USERID = System.getProperty("user.name");

    ThreadLocal<String> threadLocalUserId = ThreadLocal.withInitial(() -> SERVER_USERID);

    @Override
    public void createThreadSecurityContext(String userId, String password, String applId) {
        if (userId.equalsIgnoreCase(INVALID_VALUE) || password.equalsIgnoreCase(INVALID_VALUE)) {
            throw SECURITY_REQUEST_FAILED;
        }
        threadLocalUserId.set(userId);
    }

    @Override
    public void createThreadSecurityContextByDaemon(String userId, String applId) {
        if (userId.equalsIgnoreCase(INVALID_VALUE)) {
            throw SECURITY_REQUEST_FAILED;
        }
        threadLocalUserId.set(userId);
    }

    @Override
    public String getCurrentThreadUserId() {
        return threadLocalUserId.get();
    }

    @Override
    public void removeThreadSecurityContext() {
        threadLocalUserId.set(SERVER_USERID);
    }

    @Override
    public void afterPropertiesSet() throws Exception {
        // Identity switching here only changes a ThreadLocal; on z/OS every native call would
        // run with the server's authority, so refuse to start rather than silently escalate.
        if ("z/OS".equalsIgnoreCase(System.getProperty("os.name"))) {
            throw new IllegalStateException("The mock security provider cannot run on z/OS; start with "
                    + "--spring.profiles.active=zos so requests run under the caller's SAF identity");
        }
        platformAccessControl = new MockPlatformAccessControl();
       log.warn("The mock authorization check provider is used. This application should not be used in production");
    }
}

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

package org.zowe.zo.config;

import org.springframework.boot.actuate.health.Health;
import org.springframework.boot.actuate.health.HealthIndicator;
import org.springframework.context.annotation.Profile;
import org.springframework.stereotype.Component;
import org.zowe.zo.ffm.ZoRpcBindings;

/**
 * Health is UP only when the zo native library is loaded and initialized, so /application/health
 * (and anything that watches it) reflects whether the service can actually serve z/OS requests.
 */
@Component("zoNative")
@Profile("zos")
public class ZoNativeHealthIndicator implements HealthIndicator {

    @Override
    public Health health() {
        return ZoRpcBindings.nativeReady() ? Health.up().build() : Health.down().build();
    }
}

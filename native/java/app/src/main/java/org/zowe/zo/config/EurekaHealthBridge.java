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

import com.netflix.appinfo.InstanceInfo.InstanceStatus;
import com.netflix.discovery.EurekaClient;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.actuate.health.HealthEndpoint;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;
import org.zowe.apiml.eurekaservice.client.ApiMediationClient;

/**
 * The APIML onboarding enabler marks the instance UP at registration and never revisits it. This
 * bridges Spring Boot health into the Eureka instance status, so Discovery (and therefore the
 * Gateway) sees DOWN whenever the service cannot serve requests, e.g. the native library failed.
 */
@Slf4j
@Component
public class EurekaHealthBridge {

    private final ObjectProvider<ApiMediationClient> apiMediationClient;
    private final ObjectProvider<HealthEndpoint> healthEndpoint;

    public EurekaHealthBridge(ObjectProvider<ApiMediationClient> apiMediationClient,
                              ObjectProvider<HealthEndpoint> healthEndpoint) {
        this.apiMediationClient = apiMediationClient;
        this.healthEndpoint = healthEndpoint;
    }

    @EventListener(ApplicationReadyEvent.class)
    public void bridge() {
        ApiMediationClient client = apiMediationClient.getIfAvailable();
        HealthEndpoint health = healthEndpoint.getIfAvailable();
        EurekaClient eureka = client == null ? null : client.getEurekaClient();
        if (eureka == null || health == null) {
            log.info("APIML registration not active; Eureka health bridging skipped");
            return;
        }
        eureka.registerHealthCheck(current -> {
            String status = health.health().getStatus().getCode();
            return switch (status) {
                case "UP" -> InstanceStatus.UP;
                case "OUT_OF_SERVICE" -> InstanceStatus.OUT_OF_SERVICE;
                case "UNKNOWN" -> InstanceStatus.UNKNOWN;
                default -> InstanceStatus.DOWN;
            };
        });
        log.info("Eureka instance status now follows /application/health");
    }
}

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

package org.zowe.zo.controllers.zosmf;

import org.junit.jupiter.api.Test;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import static org.hamcrest.Matchers.containsString;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

class ZosmfInfoControllerTest {

    @Test
    void reportsZosmfShapedInfoAndIdentifiesItselfAsZo() throws Exception {
        MockMvc mvc = MockMvcBuilders
                .standaloneSetup(new ZosmfInfoController("05.29.00", "zos.example.com", "27610", "0.5.0"))
                .build();

        mvc.perform(get("/zosmf/info"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.zos_version").value("05.29.00"))
                .andExpect(jsonPath("$.zosmf_version").value("29"))
                .andExpect(jsonPath("$.zosmf_full_version").value("29.0"))
                .andExpect(jsonPath("$.zosmf_hostname").value("ZOS.EXAMPLE.COM"))
                .andExpect(jsonPath("$.zosmf_port").value("27610"))
                .andExpect(jsonPath("$.zosmf_saf_realm").value("SAFRealm"))
                .andExpect(jsonPath("$.api_version").value("1"))
                .andExpect(jsonPath("$.plugins[0].pluginDefaultName").value(containsString("this is zo, not z/OSMF")))
                .andExpect(jsonPath("$.plugins[0].pluginStatus").value("ACTIVE"));
    }
}

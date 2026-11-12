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

import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockFilterChain;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

class CsrfHeaderFilterTest {

    private MockFilterChain run(boolean required, String method, String uri, boolean withHeader, MockHttpServletResponse response)
            throws Exception {
        var request = new MockHttpServletRequest(method, uri);
        if (withHeader) request.addHeader(CsrfHeaderFilter.HEADER, "true");
        var chain = new MockFilterChain();
        new CsrfHeaderFilter(required).doFilter(request, response, chain);
        return chain;
    }

    @Test
    void rejectsStateChangingRequestWithoutHeader() throws Exception {
        var response = new MockHttpServletResponse();
        var chain = run(true, "PUT", "/zosmf/restfiles/ds/A.B", false, response);
        assertEquals(403, response.getStatus());
        assertNull(chain.getRequest());
    }

    @Test
    void allowsStateChangingRequestWithHeader() throws Exception {
        var response = new MockHttpServletResponse();
        assertNotNull(run(true, "DELETE", "/zo/api/v1/ds/A.B", true, response).getRequest());
    }

    @Test
    void allowsSafeMethodsAndOtherPaths() throws Exception {
        assertNotNull(run(true, "GET", "/zosmf/restjobs/jobs", false, new MockHttpServletResponse()).getRequest());
        assertNotNull(run(true, "POST", "/actuator/refresh", false, new MockHttpServletResponse()).getRequest());
    }

    @Test
    void canBeDisabled() throws Exception {
        assertNotNull(run(false, "POST", "/zosmf/restfiles/ds/A.B", false, new MockHttpServletResponse()).getRequest());
    }
}

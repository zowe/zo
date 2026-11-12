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

package org.zowe.zo.ffm;

import com.fasterxml.jackson.databind.JsonNode;

/**
 * In-process client for zo's JSON-RPC command dispatcher.
 *
 * Method names and param/result shapes are the zo RPC contract documented in
 * packages/sdk/src/doc/rpc (params use the same camelCase keys as the SDK types). Only
 * methods on the native allowlist are reachable; others fail with {@link ZoRpcException}
 * code {@link ZoRpcException#METHOD_NOT_FOUND}.
 *
 * Implementations live in the org.zowe.zo.ffm package so ThreadSecurityAspect runs every
 * call under the authenticated user's thread-level security environment. Callers must
 * invoke them through the Spring bean (never via self-invocation) for that to apply.
 */
public interface ZoRpcClient {

    /**
     * Invoke a zo RPC method.
     *
     * @param method RPC method name, e.g. "listDatasets"
     * @param params request params (a Map, record or POJO serialized by Jackson); may be null
     * @return the JSON-RPC "result" object (never null on success)
     * @throws ZoRpcException when the RPC returns an error
     */
    JsonNode call(String method, Object params) throws ZoRpcException;
}

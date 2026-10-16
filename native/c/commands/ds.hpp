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

#include "../parser.hpp"

namespace ds
{
using namespace plugin;
int create_with_attributes(InvocationContext &result);
int handle_data_set_create_fb(InvocationContext &result);
int handle_data_set_create_vb(InvocationContext &result);
int handle_data_set_create_adata(InvocationContext &result);
int handle_data_set_create_loadlib(InvocationContext &result);
int handle_data_set_view(InvocationContext &result);
int handle_data_set_list(InvocationContext &result);
int handle_data_set_list_members(InvocationContext &result);
int handle_data_set_resolve_alias(InvocationContext &result);
int handle_data_set_write(InvocationContext &result);
int handle_data_set_delete(InvocationContext &result);
int handle_vsam_delete(InvocationContext &result);
int handle_data_set_restore(InvocationContext &result);
int handle_data_set_compress(InvocationContext &result);
int handle_data_set_create_member(InvocationContext &result);
int handle_data_set_rename(InvocationContext &result);
int handle_data_set_copy(InvocationContext &result);
int handle_rename_member(InvocationContext &result);
void register_commands(parser::Command &root_command);
} // namespace ds

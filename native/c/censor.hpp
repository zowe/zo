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
#ifndef CENSOR_HPP
#define CENSOR_HPP

#include <string>
#include <vector>

namespace censor
{

std::vector<std::string> build_censored_options();

/**
 * @brief Censor values for known sensitive options in the provided string.
 */
std::string censor_raw_data(const std::string &data);

} // namespace censor

#endif
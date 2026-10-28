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

#ifndef ZJBTIME_HPP
#define ZJBTIME_HPP

#include <cstdint>
#include <cstdio>
#include <string>

namespace zjb_detail
{

constexpr int days_in_year(int year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0) ? 366 : 365;
}

// JES dates are packed 0cyydddF; times are local centiseconds since midnight.
inline std::string format_timestamp(const unsigned char *date, int time, int64_t utc_offset_cs)
{
  constexpr int CS_PER_DAY = 8640000;
  if (date[0] > 9 || (date[1] >> 4) > 9 || (date[1] & 0x0F) > 9 ||
      (date[2] >> 4) > 9 || (date[2] & 0x0F) > 9 || (date[3] >> 4) > 9 ||
      ((date[3] & 0x0F) != 0x0F && (date[3] & 0x0F) != 0x0C) || time < 0 || time >= CS_PER_DAY)
    return "";

  int year = 1900 + date[0] * 100 + (date[1] >> 4) * 10 + (date[1] & 0x0F);
  int day = (date[2] >> 4) * 100 + (date[2] & 0x0F) * 10 + (date[3] >> 4);
  if (day < 1 || day > days_in_year(year))
    return "";

  int64_t utc_time = static_cast<int64_t>(time) - utc_offset_cs;
  while (utc_time < 0)
  {
    utc_time += CS_PER_DAY;
    --day;
  }
  while (utc_time >= CS_PER_DAY)
  {
    utc_time -= CS_PER_DAY;
    ++day;
  }
  while (day < 1)
  {
    --year;
    day += days_in_year(year);
  }
  while (day > days_in_year(year))
  {
    day -= days_in_year(year);
    ++year;
  }

  constexpr int MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int month = 1;
  for (int month_days : MONTH_DAYS)
  {
    if (month == 2 && days_in_year(year) == 366)
      ++month_days;
    if (day <= month_days)
      break;
    day -= month_days;
    ++month;
  }

  const int centiseconds = static_cast<int>(utc_time);
  char buffer[25];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                year, month, day, centiseconds / 360000, (centiseconds / 6000) % 60,
                (centiseconds / 100) % 60, (centiseconds % 100) * 10);
  return buffer;
}

} // namespace zjb_detail

#endif

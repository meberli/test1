#include "waste_window.h"

#include <time.h>

namespace {
int64_t local_at(int64_t day, int day_shift, int minutes) {
  time_t tt = static_cast<time_t>(day);
  struct tm lt;
  localtime_r(&tt, &lt);
  lt.tm_mday += day_shift;
  lt.tm_hour = minutes / 60;
  lt.tm_min = minutes % 60;
  lt.tm_sec = 0;
  lt.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&lt));
}
}  // namespace

int64_t waste_window_start(int64_t day, uint16_t from_min) {
  if (from_min > 1439) from_min = 1439;
  return local_at(day, -1, from_min);
}

int64_t waste_window_end(int64_t day, uint16_t until_min) {
  if (until_min >= 1440) return local_at(day, 1, 0);
  return local_at(day, 0, until_min);
}

bool waste_window_active(int64_t day, int64_t now, uint16_t from_min, uint16_t until_min) {
  return now >= waste_window_start(day, from_min) && now < waste_window_end(day, until_min);
}

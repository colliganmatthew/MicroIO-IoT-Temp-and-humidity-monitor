#pragma once
#include "config.h"

// =============================================================================
// debug.h  —  project-wide debug logging macro
//
// Usage (anywhere in the project):
//
//   DBG("sensor",  "Temperature: %.1f C", temperature);
//   DBG("mqtt",    "Publishing to %s", topic);
//   DBG("scpi",    "*IDN? received");
//
// Output format:
//   [DBG][sensor] Temperature: 23.4 C
//
// When DEBUG_ENABLED = 0 in config.h, ALL DBG() calls compile away to
// nothing — zero Flash, zero RAM, zero runtime cost.
// When DEBUG_ENABLED = 1, every call goes to Serial.printf().
//
// =============================================================================

#if DEBUG_ENABLED

  // Full debug build — prints tag + formatted message
  #define DBG(tag, fmt, ...) \
      Serial.printf("[DBG][%-8s] " fmt "\n", tag, ##__VA_ARGS__)

#else

  // Production build — entire call removed by the preprocessor
  #define DBG(tag, fmt, ...) do {} while(0)

#endif

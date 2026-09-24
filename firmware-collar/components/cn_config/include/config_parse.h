#pragma once

// Helpers for menuconfig string values that default to "" (CONFIG_NOT_SET).
// An empty or malformed string is reported as not configured - never replaced by a guess.

#include <cstdint>

namespace cownect::config {

// Returns true and stores the value only if `text` is a complete, in-range integer
// (decimal or 0x-prefixed hex).
bool parse_int(const char* text, int64_t min_value, int64_t max_value, int64_t& out);

// Returns true for "0"/"1" only.
bool parse_bool01(const char* text, bool& out);

inline bool is_set(const char* text) { return text != nullptr && text[0] != '\0'; }

}  // namespace cownect::config

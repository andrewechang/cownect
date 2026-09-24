#include "config_parse.h"

#include <cerrno>
#include <cstdlib>

namespace cownect::config {

bool parse_int(const char* text, int64_t min_value, int64_t max_value, int64_t& out)
{
    if (!is_set(text)) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (v < min_value || v > max_value) {
        return false;
    }
    out = v;
    return true;
}

bool parse_bool01(const char* text, bool& out)
{
    if (!is_set(text) || text[1] != '\0') {
        return false;
    }
    if (text[0] == '0') { out = false; return true; }
    if (text[0] == '1') { out = true;  return true; }
    return false;
}

}  // namespace cownect::config

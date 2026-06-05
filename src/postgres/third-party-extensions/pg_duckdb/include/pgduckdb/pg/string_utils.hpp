#pragma once

#include <string.h>

inline bool
AreStringEqual(const char *str1, const char *str2) {
	return str1 == nullptr ? str1 == str2 : strcmp(str1, str2) == 0;
}

inline bool
IsEmptyString(const char *str) {
	return AreStringEqual(str, "");
}

inline bool
StringHasPrefix(const char *str, const char *prefix) {
	if (str == nullptr || prefix == nullptr) {
		return false;
	}
	size_t prefix_length = strlen(prefix);
	return strncmp(str, prefix, prefix_length) == 0;
}

namespace pgduckdb {
inline bool
IsDuckdbSchemaName(const char *s) {
	return StringHasPrefix(s, "ddb$");
}
} // namespace pgduckdb

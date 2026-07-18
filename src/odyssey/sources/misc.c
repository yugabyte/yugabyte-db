
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <ctype.h>

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

int pg_strncasecmp(const char *s1, const char *s2, size_t n)
{
	while (n-- > 0) {
		unsigned char ch1 = (unsigned char)*s1++;
		unsigned char ch2 = (unsigned char)*s2++;

		if (ch1 != ch2) {
			if (ch1 >= 'A' && ch1 <= 'Z')
				ch1 += 'a' - 'A';
			else if (IS_HIGHBIT_SET(ch1) && isupper(ch1))
				ch1 = tolower(ch1);

			if (ch2 >= 'A' && ch2 <= 'Z')
				ch2 += 'a' - 'A';
			else if (IS_HIGHBIT_SET(ch2) && isupper(ch2))
				ch2 = tolower(ch2);

			if (ch1 != ch2)
				return (int)ch1 - (int)ch2;
		}
		if (ch1 == 0)
			break;
	}
	return 0;
}

bool parse_bool_with_len(const char *value, size_t len, bool *result)
{
	switch (*value) {
	case 't':
	case 'T':
		if (pg_strncasecmp(value, "true", len) == 0) {
			if (result)
				*result = true;
			return true;
		}
		break;
	case 'f':
	case 'F':
		if (pg_strncasecmp(value, "false", len) == 0) {
			if (result)
				*result = false;
			return true;
		}
		break;
	case 'y':
	case 'Y':
		if (pg_strncasecmp(value, "yes", len) == 0) {
			if (result)
				*result = true;
			return true;
		}
		break;
	case 'n':
	case 'N':
		if (pg_strncasecmp(value, "no", len) == 0) {
			if (result)
				*result = false;
			return true;
		}
		break;
	case 'o':
	case 'O':
		/* 'o' is not unique enough */
		if (pg_strncasecmp(value, "on", (len > 2 ? len : 2)) == 0) {
			if (result)
				*result = true;
			return true;
		} else if (pg_strncasecmp(value, "off", (len > 2 ? len : 2)) ==
			   0) {
			if (result)
				*result = false;
			return true;
		}
		break;
	case '1':
		if (len == 1) {
			if (result)
				*result = true;
			return true;
		}
		break;
	case '0':
		if (len == 1) {
			if (result)
				*result = false;
			return true;
		}
		break;
	default:
		break;
	}

	if (result)
		*result = false; /* suppress compiler warning */
	return false;
}

bool parse_bool(const char *value, bool *result)
{
	return parse_bool_with_len(value, strlen(value), result);
}

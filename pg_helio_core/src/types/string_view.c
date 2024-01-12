/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/types/string_view.c
 *
 * Utilities that operate substring views on strings (that are not guaranteed to be
 * null terminated).
 *
 *-------------------------------------------------------------------------
 */

#include "utils/string_view.h"
#include <common/hashfn.h>

/*
 * Initializes a StringView from a null terminated C string
 */
StringView
CreateStringViewFromString(const char *string)
{
	StringView s =
	{
		.string = string,
		.length = strlen(string)
	};
	return s;
}


/*
 * Initializes a StringView from a  C string upto a length
 */
StringView
CreateStringViewFromStringWithLength(const char *string, uint32_t length)
{
	StringView s =
	{
		.string = string,
		.length = length
	};
	return s;
}


/*
 * Initializes a StringView from a text
 */
StringView
CreateStringViewFromText(const text *string)
{
	StringView s =
	{
		.string = VARDATA_ANY(string),
		.length = VARSIZE_ANY_EXHDR(string)
	};
	return s;
}


/*
 * Creates a new Null terminated C string from the given string view.
 */
char *
CreateStringFromStringView(const StringView *string)
{
	return pnstrdup(string->string, string->length);
}


/*
 * Compares two string view entries based on their length and contents.
 */
int32_t
CompareStringView(const StringView *left, const StringView *right)
{
	uint32_t minLength = left->length > right->length ? right->length : left->length;
	if (minLength != 0)
	{
		int cmp = strncmp(left->string, right->string, minLength);
		if (cmp != 0)
		{
			return cmp;
		}
	}

	return left->length - right->length;
}


/*
 * Hashes a string view based on its contents
 */
uint32_t
HashStringView(const StringView *view)
{
	return hash_bytes((const unsigned char *) view->string,
					  (int) view->length);
}


/*
 * Searches a given stringView for a given character.
 * If it's found, then returns the substring until that character
 * e.g. Given a path "a.b.c.d", and the character '.'
 * Returns "a".
 *
 * If the character is not found, returns an empty StringView.
 */
StringView
StringViewFindPrefix(const StringView *view, char upToCharacter)
{
	StringView result = { 0 };
	char *substring = memchr(view->string, upToCharacter, view->length);
	if (substring != NULL && (substring - view->string) < view->length)
	{
		result.string = view->string;
		result.length = (uint32_t) (substring - view->string);
	}

	return result;
}


/*
 * Searches a given stringView for a given character.
 * If it's found, then returns the substring after that character
 * e.g. Given a path "a.b.c.d", and the character '.'
 * Returns "b.c.d".
 *
 * If the character is not found, returns an empty StringView.
 */
StringView
StringViewFindSuffix(const StringView *view, char upToCharacter)
{
	StringView result = { 0 };
	char *substring = memchr(view->string, upToCharacter, view->length);
	if (substring != NULL && (substring - view->string) < view->length)
	{
		uint32_t substringLength = (uint32_t) ((view->string + view->length) -
											   (substring + 1));
		if (substringLength > 0)
		{
			result.string = substring + 1;
			result.length = substringLength;
		}
	}
	return result;
}


/*
 * Returns a substring of a StringView from a specified offset.
 * Requires offset to be less than the source's length.
 * e.g. given a string "a.b.c" and an offset 2, returns "b.c".
 */
StringView
StringViewSubstring(const StringView *source, uint32_t offset)
{
	if (offset > source->length)
	{
		ereport(ERROR, (errmsg("Invalid offset %u from source string of length %u",
							   offset, source->length)));
	}

	StringView result =
	{
		.string = source->string + offset,
		.length = source->length - offset,
	};
	return result;
}


/*
 * Parses a string view as a positive integer. We use this helper
 * method instead of strtoll since the path is a substring in a much larger
 * string and may or may not be null terminated.
 * Returns -1 if the path is not a valid positive integer.
 */
int32_t
StringViewToPositiveInteger(const StringView *view)
{
	if (view == NULL || view->length == 0)
	{
		return -1;
	}

	int32_t finalValue = 0;
	for (uint32_t i = 0; i < view->length; i++)
	{
		if (view->string[i] < '0' || view->string[i] > '9')
		{
			return -1;
		}

		finalValue = (finalValue * 10) + (view->string[i] - '0');
		if (finalValue < 0)
		{
			return -1;
		}
	}

	return finalValue;
}


/*
 * Returns the number of characters in a string view, where string may contain
 * multibyte characters.
 * This counts the characters that take multiple bytes to store (e.g. unicode chars)
 * as single character.
 */
uint32_t
StringViewMultiByteCharStrlen(const StringView *view)
{
	uint32_t len = 0;
	int charSize = 0, c = 0;
	const char *str = view->string;
	for (int i = 0; i < (int) view->length; i += charSize, len++)
	{
		c = (int) str[i];
		if (c >= 0 && c <= 127)
		{
			charSize = 1;
		}
		else if ((c & 0xE0) == 0xC0)
		{
			charSize = 2;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			charSize = 3;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			charSize = 4;
		}

		/* char size 5 & 6 are unnecessary in 4 byte UTF-8 */
		else
		{
			ereport(ERROR, errmsg("invalid utf8 i: %d, str: %s charSize: %d", i, str,
								  charSize));
		}
	}
	return len;
}

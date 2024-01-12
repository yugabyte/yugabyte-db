/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/string_view.h
 *
 * Utilities that operate substring views on strings (that are not guaranteed to be
 * null terminated).
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#ifndef STRING_VIEW_H
#define STRING_VIEW_H

/*
 * Represents a 'view' over a larger string.
 * Contains a pointer to the first character in the substring
 * along with a length associated with the substring
 */
typedef struct StringView
{
	const char *string;
	uint32_t length;
} StringView;


StringView CreateStringViewFromString(const char *string);

StringView CreateStringViewFromStringWithLength(const char *string,
												uint32_t length);

StringView CreateStringViewFromText(const text *string);

StringView StringViewFindPrefix(const StringView *view, char character);

StringView StringViewFindSuffix(const StringView *view, char character);

StringView StringViewSubstring(const StringView *source, uint32_t offset);

int32_t StringViewToPositiveInteger(const StringView *string);

uint32_t StringViewMultiByteCharStrlen(const StringView *view);

char * CreateStringFromStringView(const StringView *string);

int32_t CompareStringView(const StringView *left, const StringView *right);

uint32_t HashStringView(const StringView *view);

/*
 * Checks if a StringView starts with a given character.
 */
inline static bool
StringViewStartsWith(const StringView *source, char character)
{
	return source != NULL && source->length > 0 && source->string[0] == character;
}


/*
 * Checks if a StringView starts with a given substring.
 */
inline static bool
StringViewStartsWithStringView(const StringView *source, const StringView *string)
{
	Assert(string != NULL);
	return source != NULL && source->length >= string->length &&
		   strncmp(source->string, string->string, string->length) == 0;
}


/*
 * Checks if a StringView ends with a given character.
 */
inline static bool
StringViewEndsWith(const StringView *source, char character)
{
	return source != NULL && source->length > 0 && source->string[source->length - 1] ==
		   character;
}


/*
 * Checks if a StringView ends with a given string.
 */
inline static bool
StringViewEndsWithString(const StringView *source, const char *target)
{
	int32_t sourceIndex = source->length - 1;
	int32_t stringIndex = strlen(target) - 1;
	if (sourceIndex < stringIndex)
	{
		return false;
	}
	while (stringIndex >= 0)
	{
		if (source->string[sourceIndex] != target[stringIndex])
		{
			return false;
		}
		sourceIndex--;
		stringIndex--;
	}
	return true;
}


/*
 * Validates if two string views are equal (based on length and contents).
 * Does an ordinal comparison of characters.
 */
inline static bool
StringViewEquals(const StringView *left, const StringView *right)
{
	if (left == NULL && right == NULL)
	{
		return true;
	}

	if ((left == NULL) ^ (right == NULL))
	{
		return false;
	}

	return left->length == right->length &&
		   strncmp(left->string, right->string, left->length) == 0;
}


/*
 * Validates if  string views is  equal to const char string (based on length and contents).
 * Does an ordinal comparison of characters.
 */
inline static bool
StringViewEqualsCString(const StringView *left, const char *right)
{
	if (left == NULL && right == NULL)
	{
		return true;
	}

	if (left != NULL)
	{
		if (left->string == NULL && right == NULL)
		{
			return true;
		}

		if ((left->string == NULL) ^ (right == NULL))
		{
			return false;
		}
	}

	if ((left == NULL) ^ (right == NULL))
	{
		return false;
	}

	return left->length == strlen(right) &&
		   strncmp(left->string, right, left->length) == 0;
}


/*
 * Validates if  string views is  equal to const char string (based on length and contents).
 * Does an case insensitive comparison of characters.
 */
inline static bool
StringViewEqualsCStringCaseInsensitive(const StringView *left, const char *right)
{
	if (left == NULL && right == NULL)
	{
		return true;
	}

	if (left != NULL)
	{
		if (left->string == NULL && right == NULL)
		{
			return true;
		}

		if ((left->string == NULL) ^ (right == NULL))
		{
			return false;
		}
	}

	if ((left == NULL) ^ (right == NULL))
	{
		return false;
	}

	return left->length == strlen(right) &&
		   strncasecmp(left->string, right, left->length) == 0;
}


/*
 * Checks if a string view contains a given character.
 */
inline static bool
StringViewContains(const StringView *source, char character)
{
	return source != NULL && source->length > 0 && memchr(source->string, character,
														  source->length) != NULL;
}


#endif

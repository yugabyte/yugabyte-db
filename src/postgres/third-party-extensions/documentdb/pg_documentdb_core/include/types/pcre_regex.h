/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/pcre_regex.h
 *
 * Utilities for PCRE Regex evaluation
 *
 *-------------------------------------------------------------------------
 */

#ifndef PCRE_REGEX_H
#define PCRE_REGEX_H


typedef struct PcreData PcreData;

void RegexCompileDuringPlanning(char *regexPatternStr, char *options);
PcreData * RegexCompile(char *regexPatternStr, char *options);
PcreData * RegexCompileForAggregation(char *regexPatternStr, char *options,
									  bool enableNoAutoCapture,
									  const char *regexInvalidErrorMessage);
size_t * GetResultVectorUsingPcreData(PcreData *pcreData);
int GetResultLengthUsingPcreData(PcreData *pcreData);
bool IsValidRegexOptions(char *options);
void FreePcreData(PcreData *pcreData);

bool PcreRegexExecute(char *regexPatternStr, char *options,
					  PcreData *pcreData,
					  const StringView *subjectString);

#endif

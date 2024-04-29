// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations under
// the License.

#include "json_util.h"

#include "utils/fmgrprotos.h"

void
AddNumericJsonEntry(JsonbParseState *state, char *key_buf, int64 val)
{
	JsonbPair pair;
	pair.key.type = jbvString;
	pair.value.type = jbvNumeric;
	pair.key.val.string.len = strlen(key_buf);
	pair.key.val.string.val = pstrdup(key_buf);
	pair.value.val.numeric =
		DatumGetNumeric(DirectFunctionCall1(int8_numeric, val));

	(void) pushJsonbValue(&state, WJB_KEY, &pair.key);
	(void) pushJsonbValue(&state, WJB_VALUE, &pair.value);
}

void
AddBoolJsonEntry(JsonbParseState *state, char *key_buf, bool val)
{
	JsonbPair pair;
	pair.key.type = jbvString;
	pair.value.type = jbvBool;
	pair.key.val.string.len = strlen(key_buf);
	pair.key.val.string.val = pstrdup(key_buf);
	pair.value.val.boolean = val;

	(void) pushJsonbValue(&state, WJB_KEY, &pair.key);
	(void) pushJsonbValue(&state, WJB_VALUE, &pair.value);
}

void
AddStringJsonEntry(JsonbParseState *state, char *key_buf, const char *val)
{
	JsonbPair pair;
	pair.key.type = jbvString;
	pair.value.type = jbvString;
	pair.key.val.string.len = strlen(key_buf);
	pair.key.val.string.val = pstrdup(key_buf);
	pair.value.val.string.len = strlen(val);
	pair.value.val.string.val = pstrdup(val);

	(void) pushJsonbValue(&state, WJB_KEY, &pair.key);
	(void) pushJsonbValue(&state, WJB_VALUE, &pair.value);
}

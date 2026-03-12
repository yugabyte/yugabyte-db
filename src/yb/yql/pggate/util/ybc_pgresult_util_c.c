// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

// This file is compiled as C (not C++) so it can include libpq-int.h, which
// relies on postgres-internal type definitions that are not C++ compatible.

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "libpq-int.h"

#include <stddef.h>
#include <string.h>

void YBCPgSaveMessageField(PGresult *res, char code, const char *value) {
  PGMessageField *pfield;
  size_t vlen = strlen(value) + 1;
  pfield = (PGMessageField *)
      malloc(offsetof(PGMessageField, contents) + vlen);
  if (!pfield)
    return;
  pfield->code = code;
  memcpy(pfield->contents, value, vlen);
  pfield->next = res->errFields;
  res->errFields = pfield;
}

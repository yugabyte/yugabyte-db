/*--------------------------------------------------------------------------------------------------
 *
 * yb_fdw.h
 *	  prototypes for yb_fdw.h
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/executor/yb_fdw.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

extern Datum yb_fdw_handler();

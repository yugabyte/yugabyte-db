/* -------------------------------------------------------------------------
 *
 * yb_file_utils.h
 * 	  Declarations for functions used for reading and writing Yugabyte temporary files.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 * 	  src/include/yb_file_utils.h
 * -------------------------------------------------------------------------
 */

#ifndef YB_FILE_UTILS_H
#define YB_FILE_UTILS_H

#include "c.h"

extern void
yb_write_struct_to_file(const char *tempfile_name, const char *file_name,
						void *struct_ptr, size_t struct_size, int32 format_id);

extern void
yb_read_struct_from_file(const char *file_name, void *struct_ptr,
						 size_t struct_size, int32 expected_format_id);

#endif							/* YB_FILE_UTILS_H */

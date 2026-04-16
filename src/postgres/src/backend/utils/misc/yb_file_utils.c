/* -------------------------------------------------------------------------
 *
 * yb_file_utils.c
 *	  Functions used for reading and writing Yugabyte structures to/from temporary files.
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
 *	  src/backend/utils/utils/misc/yb_file_utils.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include <unistd.h>

#include "storage/fd.h"
#include "yb_file_utils.h"

static void write_chunk(FILE *fpout, void *ptr, size_t len);
static bool read_chunk(FILE *fpin, void *ptr, size_t len);

static void
write_chunk(FILE *fpout, void *ptr, size_t len)
{
	fwrite(ptr, len, 1, fpout);
}

static bool
read_chunk(FILE *fpin, void *ptr, size_t len)
{
	return fread(ptr, 1, len, fpin) == len;
}

/*
 * Writes structures to file given temporary and permenant file paths, pointer to the structure,
 * size of the structure and a format_id. Format ID is used to denote the version of the structure
 * and is used as a header to the file to which the struct is being written. If changes are made to
 * the structure which needs to be written the format_id must also be changed allowing the user to
 * differentiate between files storing different versions of the same structure. We write the changes
 * to a temporary yb file and then rename the file to make it the new permanent yb file to protect
 * the exisiting permanent yb file from corruption due to errors that might occur while writing the
 * new changes.
 */
void
yb_write_struct_to_file(const char *tempfile_name, const char *file_name,
						void *struct_ptr, size_t struct_size, int32 format_id)
{
	FILE	   *temp_fpout;

	if (!struct_ptr)
		return;

	temp_fpout = AllocateFile(tempfile_name, PG_BINARY_W);
	if (temp_fpout == NULL)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not open yb file \"%s\": %m",
						tempfile_name)));
		return;
	}

	if (!struct_ptr)
		return;

	write_chunk(temp_fpout, &format_id, sizeof(format_id));

	write_chunk(temp_fpout, struct_ptr, struct_size);

	if (ferror(temp_fpout))
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not write temporary yb file \"%s\": %m",
						tempfile_name)));
		FreeFile(temp_fpout);
		unlink(tempfile_name);
	}
	else if (FreeFile(temp_fpout) < 0)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not close temporary yb file \"%s\": %m",
						tempfile_name)));
		unlink(tempfile_name);
	}
	else if (rename(tempfile_name, file_name) < 0)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not rename temporary yb file \"%s\" to \"%s\": %m",
						tempfile_name, file_name)));
		unlink(tempfile_name);
	}
}

/*
 * Reads structures from a file given the path to file, pointer to structure, size of the structure
 * and the expected format_id. Expected format ID is used to make sure that the file being read has
 * the correct version of the structure allowing the user to avoid errors whcih may occur due to
 * reading a file contaning a previous version of the same structure.
 */
void
yb_read_struct_from_file(const char *file_name, void *struct_ptr,
						 size_t struct_size, int32 expected_format_id)
{
	FILE	   *fpin;
	int32		format_id;

	if ((fpin = AllocateFile(file_name, PG_BINARY_R)) == NULL)
		return;

	if (!read_chunk(fpin, &format_id, sizeof(format_id)) || format_id != expected_format_id
		|| !read_chunk(fpin, struct_ptr, struct_size))
		ereport(LOG, (errmsg("corrupted yb file \"%s\"", file_name)));

	FreeFile(fpin);

	elog(DEBUG2, "removing permanent yb file \"%s\"", file_name);
	unlink(file_name);
}

/*-------------------------------------------------------------------------
 *
 * ybbitmatrix.c
 *	  Yugabyte bit matrix package
 *
 * This module provides a boolean matrix data structure that is internally
 * implemented as a bitmapset (nodes/bitmapset.h). This allows for efficient
 * storage and row-level operations.
 *
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
 * IDENTIFICATION
 *	  src/backend/nodes/ybbitmatrix.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <inttypes.h>

#include "nodes/ybbitmatrix.h"

static inline void
check_matrix_invariants(const YbBitMatrix *matrix)
{
	if (!matrix || !matrix->data)
		elog(ERROR, "YB bitmatrix not defined");
}

static inline void
check_column_invariants(const YbBitMatrix *matrix, int col_idx)
{
	if (col_idx >= matrix->ncols)
		elog(ERROR,
			 "column index must be between [0, %" PRId32 "] in YB bitmatrix",
			 matrix->ncols - 1);
}

static inline void
check_row_invariants(const YbBitMatrix *matrix, int row_idx)
{
	if (row_idx >= matrix->nrows)
		elog(ERROR,
			 "row index must be between [0, %" PRId32 "] in YB bitmatrix",
			 matrix->nrows - 1);
}

void
YbInitBitMatrix(YbBitMatrix *matrix, int nrows, int ncols)
{
	Assert(!matrix->data);

	matrix->nrows = nrows;
	matrix->ncols = ncols;
	const int max_idx = matrix->nrows * matrix->ncols;
	if (max_idx)
		matrix->data = bms_del_member(bms_add_member(NULL, max_idx), max_idx);
}

void
YbCopyBitMatrix(YbBitMatrix *dest, const YbBitMatrix *src)
{
	*dest = *src;
	dest->data = bms_copy(src->data);
}

void
YbFreeBitMatrix(YbBitMatrix *matrix)
{
	bms_free(matrix->data);
}

int
YbBitMatrixNumRows(const YbBitMatrix *matrix)
{
	return matrix->nrows;
}

int
YbBitMatrixNumCols(const YbBitMatrix *matrix)
{
	return matrix->ncols;
}

void
YbBitMatrixSetRow(YbBitMatrix *matrix, int row_idx, bool value)
{
	check_matrix_invariants(matrix);
	check_row_invariants(matrix, row_idx);

	int lower = row_idx * matrix->ncols;
	int upper = lower + matrix->ncols - 1;

	/*
	 * TODO(kramanathan): Optimize extra palloc in setting values to false.
	 * This can be done be implementing a bms_del_range function.
	 */
	matrix->data = value ? bms_add_range(matrix->data, lower, upper) :
						   bms_del_members(matrix->data,
										   bms_add_range(NULL, lower, upper));
}

/*
 * This function returns the smallest row greater than "prev_row" that has a
 * member in the given column, or -2 if there is none.
 * "prev_row" must NOT be less than -1, or the behavior is unpredictable.
 *
 * This is intended as support for iterating through the members of a set.
 * The typical pattern is
 *
 *		x = -1;
 *		while ((x = YbBitMatrixNextMemberInColumn(inputmatrix, col, row_x)) >= 0)
 *			process member row_x;
 *
 * Notice that when there are no more members, we return -2, not -1 as you
 * might expect. The rationale for that is to allow distinguishing the
 * loop-not-started state (x == -1) from the loop-completed state (x == -2).
 * It makes no difference in simple loop usage, but complex iteration logic
 * might need such an ability.
 * This semantics of this function are based on bms_next_member/bms_prev_member
 * in bitmapset.c.
 */
int
YbBitMatrixNextMemberInColumn(const YbBitMatrix *matrix, int col_idx,
							  int prev_row)
{
	check_matrix_invariants(matrix);
	check_column_invariants(matrix, col_idx);

	for (int row_idx = prev_row + 1, cell_idx = (row_idx * matrix->ncols) + col_idx;
		 row_idx < matrix->nrows;
		 row_idx++, cell_idx += matrix->ncols)
	{
		if (bms_is_member(cell_idx, matrix->data))
			return row_idx;
	}

	return -2;
}

/*
 * This function returns the smallest column greater than "prev_col" that has a
 * member in the given row, or -2 if there is none.
 * "prev_col" must NOT be less than -1, or the behavior is unpredictable.
 *
 * For semantics, see note on YbBitMatrixNextMemberInColumn.
 */
int
YbBitMatrixNextMemberInRow(const YbBitMatrix *matrix, int row_idx, int prev_col)
{
	check_matrix_invariants(matrix);
	check_row_invariants(matrix, row_idx);

	int prevbit = (row_idx * matrix->ncols) + prev_col;
	int nextbit = bms_next_member(matrix->data, prevbit);
	int result = nextbit - prevbit + prev_col;
	if (result > prev_col && result < matrix->ncols)
		return result;

	return -2;
}

bool
YbBitMatrixGetValue(const YbBitMatrix *matrix, int row_idx, int col_idx)
{
	check_matrix_invariants(matrix);
	check_column_invariants(matrix, col_idx);
	check_row_invariants(matrix, row_idx);

	return bms_is_member((row_idx * matrix->ncols) + col_idx, matrix->data);
}

void
YbBitMatrixSetValue(YbBitMatrix *matrix, int row_idx, int col_idx, bool value)
{
	check_matrix_invariants(matrix);
	check_column_invariants(matrix, col_idx);
	check_row_invariants(matrix, row_idx);

	const int idx = (row_idx * matrix->ncols) + col_idx;
	matrix->data = value ? bms_add_member(matrix->data, idx) :
						   bms_del_member(matrix->data, idx);
}

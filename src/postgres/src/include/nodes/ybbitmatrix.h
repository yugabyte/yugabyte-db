/*-------------------------------------------------------------------------
 *
 * ybbitmatrix.h
 *	  Yugabyte boolean bitmap matrix package
 *
 * This module provides a boolean matrix data structure that is internally
 * implemented as a bitmapset (nodes/bitmapset.h). This allows for efficient
 * storage and row-level operations.
 *
 *
 * Copyright (c) 2003-2018, PostgreSQL Global Development Group
 *
 * src/include/nodes/ybtidbitmatrix.h
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "nodes/bitmapset.h"

/*
 * YbBitMatrix is a bitmapset implementation of a generic two-dimensional
 * matrix that is composed of (nrows * ncols) of boolean data.
 */
typedef struct
{
	int nrows; /* Number of rows in the matrix */
	int ncols; /* Number of columns in the matrix */

	/* The data held in the matrix implemented as a one-dimensional BMS */
	Bitmapset *data;
} YbBitMatrix;

/* Lifecycle operations */
extern void YbInitBitMatrix(YbBitMatrix *matrix, int nrows, int ncols);
extern void YbCopyBitMatrix(YbBitMatrix *dest, const YbBitMatrix *src);
extern void YbFreeBitMatrix(YbBitMatrix *matrix);

/* General operations */
extern int YbBitMatrixNumRows(const YbBitMatrix *matrix);
extern int YbBitMatrixNumCols(const YbBitMatrix *matrix);

/* Row-level operations */
extern void YbBitMatrixSetRow(YbBitMatrix *matrix, int row_idx, bool value);
extern int YbBitMatrixNextMemberInColumn(const YbBitMatrix *matrix, int col_idx,
										 int prev_row);
extern int YbBitMatrixNextMemberInRow(const YbBitMatrix *matrix, int row_idx,
									  int prev_col);

/* Individual cell-level operations */
extern bool YbBitMatrixGetValue(const YbBitMatrix *matrix, int row_idx,
								int col_idx);
extern void YbBitMatrixSetValue(YbBitMatrix *matrix, int row_idx, int col_idx,
								bool value);

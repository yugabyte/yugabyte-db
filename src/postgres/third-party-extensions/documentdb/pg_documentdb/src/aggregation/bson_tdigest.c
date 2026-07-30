/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_tdigest.c
 *
 * Helper functions to implement t-digest.
 *
 *
 * This code contains modifications from the t-digest implementation by Tomas Vondra
 * (Copyright (C) Tomas Vondra, 2019)
 * The original code can be found at: https://github.com/tvondra/tdigest
 *
 * Copyright (c) 2019, Tomas Vondra (tomas.vondra@postgresql.org).
 *
 * Permission to use, copy, modify, and distribute this software and its documentation
 * for any purpose, without fee, and without a written agreement is hereby granted,
 * provided that the above copyright notice and this paragraph and the following two
 * paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL $ORGANISATION BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL,
 * INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE
 * OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF TOMAS VONDRA HAS BEEN ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * TOMAS VONDRA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND $ORGANISATION HAS NO
 * OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <utils/array.h>
#include <utils/float.h>
#include <catalog/pg_type.h>
#include <utils/lsyscache.h>

#include "utils/documentdb_errors.h"
#include "io/bson_core.h"

/*
 * A centroid, used both for in-memory and on-disk storage.
 */
typedef struct centroid_t
{
	double mean;
	int64 count;
} centroid_t;

/*
 * On-disk representation of the t-digest.
 */
typedef struct tdigest_t
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	int32 flags;                /* Allocated specifically for potential future purposes such as version control */
	int64 count;                /* number of items added to the t-digest */
	int compression;            /* compression used to build the digest */
	int ncentroids;             /* Number of centroids in the array */
	centroid_t centroids[FLEXIBLE_ARRAY_MEMBER];
} tdigest_t;

/*
 * Centroids used to store (sum,count), but we want to store (mean,count)
 * because that allows us to prevent rounding errors e.g. when merging
 * centroids with the same mean, or adding the same value to the centroid.
 *
 * To handle existing tdigest data in backwards-compatible way, we have
 * a flag marking the new ones with mean, and we convert the old values.
 */
#define TDIGEST_STORES_MEAN 0x0001

/*
 * An aggregate state, representing the t-digest and some additional info
 * (requested percentiles, ...).
 *
 * When adding new values to the t-digest, we add them as centroids into a
 * separate "uncompacted" part of the array. While centroids need more space
 * than plain points (24B vs. 8B), making the aggregate state quite a bit
 * larger, it does simplify the code quite a bit as it only needs to deal
 * with single struct type instead of two (centroids + points). But maybe
 * we should separate those two things in the future.
 *
 * XXX We only ever use one of values/percentiles, never both at the same
 * time. In the future the values may use a different data types than double
 * (e.g. numeric), so we keep both fields.
 */
typedef struct tdigest_aggstate_t
{
	/* basic t-digest fields (centroids at the end) */
	int64 count;                /* number of samples in the digest */
	int ncompactions;           /* number of merges/compactions */
	int compression;            /* compression algorithm */
	int ncentroids;             /* Total count of centroids */
	int ncompacted;             /* compacted part */
	/* array of requested percentiles and values */
	int npercentiles;           /* Count of percentiles requested */
	int nvalues;                /* Count of provided values */
	double trim_low;            /* low threshold (for trimmed aggs) */
	double trim_high;           /* high threshold (for trimmed aggs) */
	double *percentiles;        /* array of percentiles (if any) */
	double *values;             /* array of values (if any) */
	centroid_t *centroids;      /* centroids for the digest */
} tdigest_aggstate_t;

static int centroid_cmp(const void *a, const void *b);

#define PG_GETARG_TDIGEST(x) (tdigest_t *) PG_DETOAST_DATUM(PG_GETARG_DATUM(x))

/*
 * Size of buffer for incoming data, as a multiple of the compression value.
 * Quoting from the t-digest paper:
 *
 * The constant of proportionality should be determined by experiment, but
 * micro-benchmarks indicate that C2/C1 is in the range from 5 to 20 for
 * a single core of an Intel i7 processor. In these micro-benchmarks,
 * increasing the buffer size to (10 * delta) dramatically improves the
 * average speed but further buffer size increases have much less effect.
 *
 * XXX Maybe make the coefficient user-defined, with some reasonable limits
 * (say 2 - 20), so that users can pick the right trade-off between speed
 * and memory usage.
 */
#define BUFFER_SIZE(compression) (10 * (compression))
#define AssertBounds(index, length) Assert((index) >= 0 && (index) < (length))

#define MIN_COMPRESSION 10
#define MAX_COMPRESSION 10000


/* prototypes */
PG_FUNCTION_INFO_V1(tdigest_add_double);
PG_FUNCTION_INFO_V1(tdigest_add_double_array);
PG_FUNCTION_INFO_V1(tdigest_percentile);
PG_FUNCTION_INFO_V1(tdigest_array_percentiles);

PG_FUNCTION_INFO_V1(tdigest_serial);
PG_FUNCTION_INFO_V1(tdigest_deserial);
PG_FUNCTION_INFO_V1(tdigest_combine);

Datum tdigest_add_double(PG_FUNCTION_ARGS);
Datum tdigest_add_double_array(PG_FUNCTION_ARGS);
Datum tdigest_percentile(PG_FUNCTION_ARGS);
Datum tdigest_array_percentiles(PG_FUNCTION_ARGS);

Datum tdigest_serial(PG_FUNCTION_ARGS);
Datum tdigest_deserial(PG_FUNCTION_ARGS);
Datum tdigest_combine(PG_FUNCTION_ARGS);

static Datum double_array_to_bson_array(FunctionCallInfo fcinfo, double *darray, int len);
static double * bson_to_double_array(FunctionCallInfo fcinfo, bson_value_t *barray,
									 int *len);


static void
reverse_centroids(centroid_t *centroids, int ncentroids)
{
	int start = 0,
		end = (ncentroids - 1);

	while (start < end)
	{
		centroid_t tmp = centroids[start];
		centroids[start] = centroids[end];
		centroids[end] = tmp;

		start++;
		end--;
	}
}


static void
rebalance_centroids(centroid_t *centroids, int ncentroids,
					int64 weight_before, int64 weight_after)
{
	double ratio = weight_before / (double) weight_after;
	int64 count_before = 0;
	int64 count_after = 0;
	int start = 0;
	int end = (ncentroids - 1);
	int i;

	centroid_t *scratch = palloc(sizeof(centroid_t) * ncentroids);

	i = 0;
	while (i < ncentroids)
	{
		while (i < ncentroids)
		{
			scratch[start] = centroids[i];
			count_before += centroids[i].count;
			i++;
			start++;

			if (count_before > count_after * ratio)
			{
				break;
			}
		}

		while (i < ncentroids)
		{
			scratch[end] = centroids[i];
			count_after += centroids[i].count;
			i++;
			end--;

			if (count_before < count_after * ratio)
			{
				break;
			}
		}
	}

	memcpy(centroids, scratch, sizeof(centroid_t) * ncentroids);
	pfree(scratch);
}


/*
 * Sort centroids in the digest.
 *
 * We have to sort the whole array, because we don't just simply sort the
 * centroids - we do the rebalancing of items with the same mean too.
 */
static void
tdigest_sort(tdigest_aggstate_t *state)
{
	int i;
	int64 count_so_far;
	int64 next_group;
	int64 median_count;

	/* do qsort on the non-sorted part */
	pg_qsort(state->centroids,
			 state->ncentroids,
			 sizeof(centroid_t), centroid_cmp);

	/*
	 * The centroids are sorted by (mean,count). That's fine for centroids up
	 * to median, but above median this ordering is incorrect for centroids
	 * with the same mean (or for groups crossing the median boundary). To fix
	 * this we 'rebalance' those groups. Those entirely above median can be
	 * simply sorted in the opposite order, while those crossing the median
	 * need to be rebalanced depending on what part is below/above median.
	 */
	count_so_far = 0;
	next_group = 0; /* includes count_so_far */
	median_count = (state->count / 2);

	/*
	 * Split the centroids into groups with the same mean, process each group
	 * depending on whether it falls before/after median.
	 */
	i = 0;
	while (i < state->ncentroids)
	{
		int j = i;
		int group_size = 0;

		/* determine the end of the group */
		while ((j < state->ncentroids) &&
			   (state->centroids[i].mean == state->centroids[j].mean))
		{
			next_group += state->centroids[j].count;
			group_size++;
			j++;
		}

		/*
		 * We can ignore groups of size 1 (Total count of centroids, not counts), as
		 * those are trivially sorted.
		 */
		if (group_size > 1)
		{
			if (count_so_far >= median_count)
			{
				/* group fully above median - reverse the order */
				reverse_centroids(&state->centroids[i], group_size);
			}
			else if (next_group >= median_count)    /* group split by median */
			{
				rebalance_centroids(&state->centroids[i], group_size,
									median_count - count_so_far,
									next_group - median_count);
			}
		}

		i = j;
		count_so_far = next_group;
	}
}


/*
 * Perform compaction of the t-digest, i.e. merge the centroids as required
 * by the compression parameter.
 *
 * We always keep the data sorted in ascending order. This way we can reuse
 * the sort between compactions, and also when computing the quantiles.
 *
 * XXX Switch the direction regularly, to eliminate possible bias and improve
 * accuracy, as mentioned in the paper.
 *
 * XXX This initially used the k1 scale function, but the implementation was
 * not limiting the Total count of centroids for some reason (it might have been
 * a bug in the implementation, of course). The current code is a modified
 * copy from ajwerner [1], and AFAIK it's the k2 function, it's much simpler
 * and generally works quite nicely.
 *
 * [1] https://github.com/ajwerner/tdigestc/blob/master/go/tdigest.c
 */
static void
tdigest_compact(tdigest_aggstate_t *state)
{
	int i;

	int cur;            /* current centroid */
	int64 count_so_far;
	int64 total_count;
	double denom;
	double normalizer;
	int start;
	int step;
	int n;

	/* if the digest is fully compacted, it's been already compacted */
	if (state->ncompacted == state->ncentroids)
	{
		return;
	}

	tdigest_sort(state);

	state->ncompactions++;

	if (state->ncompactions % 2 == 0)
	{
		start = 0;
		step = 1;
	}
	else
	{
		start = state->ncentroids - 1;
		step = -1;
	}

	total_count = state->count;
	denom = 2 * M_PI * total_count * log(total_count);
	normalizer = state->compression / denom;

	cur = start;
	count_so_far = 0;
	n = 1;

	for (i = start + step; (i >= 0) && (i < state->ncentroids); i += step)
	{
		int64 proposed_count;
		double q0;
		double q2;
		double z;
		bool should_add;

		proposed_count = state->centroids[cur].count + state->centroids[i].count;

		z = proposed_count * normalizer;
		q0 = count_so_far / (double) total_count;
		q2 = (count_so_far + proposed_count) / (double) total_count;

		should_add = (z <= (q0 * (1 - q0))) && (z <= (q2 * (1 - q2)));

		if (should_add)
		{
			/*
			 * If both centroids have the same mean, don't calculate it again.
			 * The recaulculation may cause rounding errors, so that the means
			 * would drift apart over time. We want to keep them equal for as
			 * long as possible.
			 */
			if (state->centroids[cur].mean != state->centroids[i].mean)
			{
				double sum;
				int64 count;

				sum = state->centroids[i].count * state->centroids[i].mean;
				sum += state->centroids[cur].count * state->centroids[cur].mean;

				count = state->centroids[i].count;
				count += state->centroids[cur].count;

				state->centroids[cur].mean = (sum / count);
			}

			/* XXX Do this after possibly recalculating the mean. */
			state->centroids[cur].count += state->centroids[i].count;
		}
		else
		{
			count_so_far += state->centroids[cur].count;
			cur += step;
			n++;
			state->centroids[cur] = state->centroids[i];
		}

		if (cur != i)
		{
			state->centroids[i].count = 0;
			state->centroids[i].mean = 0;
		}
	}

	state->ncentroids = n;
	state->ncompacted = state->ncentroids;

	if (step < 0)
	{
		memmove(state->centroids, &state->centroids[cur], n * sizeof(centroid_t));
	}

	Assert(state->ncentroids < BUFFER_SIZE(state->compression));
}


/*
 * Estimate requested quantiles from the t-digest agg state.
 */
static void
tdigest_compute_quantiles(tdigest_aggstate_t *state, double *result)
{
	int i, j;

	/*
	 * Trigger a compaction, which also sorts the data.
	 *
	 * XXX maybe just do a sort here, which should give us a bit more accurate
	 * results, probably.
	 */
	tdigest_compact(state);

	for (i = 0; i < state->npercentiles; i++)
	{
		double count;
		double delta;
		double goal = (state->percentiles[i] * state->count);
		bool on_the_right;
		centroid_t *prev, *next;
		centroid_t *c = NULL;
		double slope;

		/* first centroid for percentile 1.0 */
		if (state->percentiles[i] == 0.0)
		{
			c = &state->centroids[0];
			result[i] = c->mean;
			continue;
		}

		/* last centroid for percentile 1.0 */
		if (state->percentiles[i] == 1.0)
		{
			c = &state->centroids[state->ncentroids - 1];
			result[i] = c->mean;
			continue;
		}

		/* walk throught the centroids and count number of items */
		count = 0;
		bool perfect_match = false;
		for (j = 0; j < state->ncentroids; j++)
		{
			c = &state->centroids[j];

			/* have we exceeded the expected count? */
			if (count + c->count > goal)
			{
				break;
			}
			else if (count + c->count == goal && c->count == 1)
			{
				result[i] = c->mean;
				perfect_match = true;
				break;
			}

			/* Account for the centroid by adding its count */
			count += c->count;
		}

		delta = goal - count - (c->count / 2.0);

		if (perfect_match)
		{
			continue;
		}

		/*
		 * double arithmetics, so don't compare to 0.0 direcly, it's enough
		 * to be "close enough"
		 */
		if (fabs(delta) < 0.000000001)
		{
			result[i] = c->mean;
			continue;
		}

		on_the_right = (delta > 0.0);

		/*
		 * for extreme percentiles we might end on the right of the last node or on the
		 * left of the first node, instead of interpolating we return the mean of the node
		 */
		if ((on_the_right && (j + 1) >= state->ncentroids) ||
			(!on_the_right && (j - 1) < 0))
		{
			result[i] = c->mean;
			continue;
		}

		if (on_the_right)
		{
			prev = &state->centroids[j];
			AssertBounds(j + 1, state->ncentroids);
			next = &state->centroids[j + 1];
			count += (prev->count / 2.0);
		}
		else
		{
			AssertBounds(j - 1, state->ncentroids);
			prev = &state->centroids[j - 1];
			next = &state->centroids[j];
			count -= (prev->count / 2.0);
		}

		/* handle infinity and -infinity cases to prevent nan results */
		if (isinf(prev->mean) && isinf(next->mean))
		{
			/* if both are infinity, return infinity depending on the goal is on the right or left */
			result[i] = (goal - count) > 0.5 ? next->mean : prev->mean;
			continue;
		}
		else if (isinf(prev->mean))
		{
			/* if prev is infinity, return infinity */
			result[i] = prev->mean;
			continue;
		}
		else if (isinf(next->mean))
		{
			/* if next is infinity, return infinity */
			result[i] = next->mean;
			continue;
		}

		slope = (next->mean - prev->mean) / (next->count / 2.0 + prev->count / 2.0);

		result[i] = prev->mean + slope * (goal - count);
	}
}


/* add a value to the t-digest, trigger a compaction if full */
static void
tdigest_add(tdigest_aggstate_t *state, double v)
{
	int compression = state->compression;
	int ncentroids = state->ncentroids;

	/* make sure we have space for the value */
	Assert(state->ncentroids < BUFFER_SIZE(compression));

	/* for a single point, the value is both sum and mean */
	state->centroids[ncentroids].count = 1;
	state->centroids[ncentroids].mean = v;
	state->ncentroids++;
	state->count++;

	Assert(state->ncentroids <= BUFFER_SIZE(compression));

	/* if the buffer got full, trigger compaction here so that next
	 * insert has free space */
	if (state->ncentroids == BUFFER_SIZE(compression))
	{
		tdigest_compact(state);
	}
}


/*
 * allocate a tdigest aggregate state, along with space for percentile(s)
 * and value(s) requested when calling the aggregate function
 */
static tdigest_aggstate_t *
tdigest_aggstate_allocate(int npercentiles, int nvalues, int compression)
{
	Size len;
	tdigest_aggstate_t *state;
	char *ptr;

	/* At least one of the provided values is equal to zero */
	Assert(nvalues == 0 || npercentiles == 0);

	/*
	 * We allocate a single chunk for the struct including percentiles and
	 * centroids (including extra buffer for new data).
	 */
	len = MAXALIGN(sizeof(tdigest_aggstate_t)) +
		  MAXALIGN(sizeof(double) * npercentiles) +
		  MAXALIGN(sizeof(double) * nvalues) +
		  (BUFFER_SIZE(compression) * sizeof(centroid_t));

	ptr = palloc0(len);

	state = (tdigest_aggstate_t *) ptr;
	ptr += MAXALIGN(sizeof(tdigest_aggstate_t));

	state->nvalues = nvalues;
	state->npercentiles = npercentiles;
	state->compression = compression;

	if (npercentiles > 0)
	{
		state->percentiles = (double *) ptr;
		ptr += MAXALIGN(sizeof(double) * npercentiles);
	}

	if (nvalues > 0)
	{
		state->values = (double *) ptr;
		ptr += MAXALIGN(sizeof(double) * nvalues);
	}

	state->centroids = (centroid_t *) ptr;
	ptr += (BUFFER_SIZE(compression) * sizeof(centroid_t));

	Assert(ptr == (char *) state + len);

	return state;
}


/* check that the requested percentiles are valid */
static void
check_percentiles(double *percentiles, int npercentiles)
{
	int i;

	for (i = 0; i < npercentiles; i++)
	{
		if ((percentiles[i] < 0.0) || (percentiles[i] > 1.0))
		{
			elog(ERROR, "invalid percentile value %f, should be in [0.0, 1.0]",
				 percentiles[i]);
		}
	}
}


static void
check_compression(int compression)
{
	if (compression < MIN_COMPRESSION || compression > MAX_COMPRESSION)
	{
		elog(ERROR, "Compression value %d is invalid", compression);
	}
}


/*
 * Add a value to the tdigest (create one if needed). Transition function
 * for tdigest aggregate with a single percentile.
 */
Datum
tdigest_add_double(PG_FUNCTION_ARGS)
{
	tdigest_aggstate_t *state;

	MemoryContext aggcontext;

	/* cannot be called directly because of internal-type argument */
	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		elog(ERROR, "tdigest_add_double called in non-aggregate context");
	}

	/*
	 * We want to skip NULL values altogether - we return either the existing
	 * t-digest (if it already exists) or NULL.
	 */
	if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}

		/* if there already is a state accumulated, don't forget it */
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	/* if there's no digest allocated, create it now */
	if (PG_ARGISNULL(0))
	{
		int compression = PG_GETARG_INT32(2);
		int npercentiles = 1;
		MemoryContext oldcontext;

		check_compression(compression);

		oldcontext = MemoryContextSwitchTo(aggcontext);

		pgbson *percentilesPgbson = PG_GETARG_MAYBE_NULL_PGBSON(3);
		if (percentilesPgbson == NULL || IsPgbsonEmptyDocument(percentilesPgbson))
		{
			PG_RETURN_NULL();
		}
		pgbsonelement percentilesPgbsonElement;
		PgbsonToSinglePgbsonElement(percentilesPgbson, &percentilesPgbsonElement);
		double percentile = percentilesPgbsonElement.bsonValue.value.v_double;

		state = tdigest_aggstate_allocate(npercentiles, 0, compression);

		state->percentiles[0] = percentile;

		MemoryContextSwitchTo(oldcontext);
	}
	else
	{
		state = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);
	}

	pgbson *inputPgbson = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (inputPgbson == NULL || IsPgbsonEmptyDocument(inputPgbson))
	{
		PG_RETURN_NULL();
	}
	pgbsonelement inputPgbsonElement;
	PgbsonToSinglePgbsonElement(inputPgbson, &inputPgbsonElement);
	if (BsonValueIsNumber(&inputPgbsonElement.bsonValue) && !IsBsonValueNaN(
			&inputPgbsonElement.bsonValue))
	{
		/* doesn't throw error if data exceeds the range of double */
		/* take overflow and underflow as infinity */
		double value = BsonValueAsDoubleQuiet(&inputPgbsonElement.bsonValue);
		tdigest_add(state, value);
	}

	PG_RETURN_POINTER(state);
}


/*
 * Add a value to the tdigest (create one if needed). Transition function
 * for tdigest aggregate with an array of percentiles.
 */
Datum
tdigest_add_double_array(PG_FUNCTION_ARGS)
{
	tdigest_aggstate_t *state;

	MemoryContext aggcontext;

	/* cannot be called directly because of internal-type argument */
	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		elog(ERROR, "tdigest_add_double_array called in non-aggregate context");
	}

	/*
	 * We want to skip NULL values altogether - we return either the existing
	 * t-digest or NULL.
	 */
	if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}

		/* if there already is a state accumulated, don't forget it */
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	/* if there's no digest allocated, create it now */
	if (PG_ARGISNULL(0))
	{
		int compression = PG_GETARG_INT32(2);
		double *percentiles;
		int npercentiles;
		MemoryContext oldcontext;

		check_compression(compression);

		oldcontext = MemoryContextSwitchTo(aggcontext);

		pgbson *percentilesPgbson = PG_GETARG_MAYBE_NULL_PGBSON(3);
		if (percentilesPgbson == NULL || IsPgbsonEmptyDocument(percentilesPgbson))
		{
			PG_RETURN_NULL();
		}
		pgbsonelement percentilesPgbsonElement;
		PgbsonToSinglePgbsonElement(percentilesPgbson, &percentilesPgbsonElement);

		percentiles = bson_to_double_array(fcinfo, &percentilesPgbsonElement.bsonValue,
										   &npercentiles);

		check_percentiles(percentiles, npercentiles);

		state = tdigest_aggstate_allocate(npercentiles, 0, compression);

		memcpy(state->percentiles, percentiles, sizeof(double) * npercentiles);

		pfree(percentiles);

		MemoryContextSwitchTo(oldcontext);
	}
	else
	{
		state = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);
	}

	pgbson *inputPgbson = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (inputPgbson == NULL || IsPgbsonEmptyDocument(inputPgbson))
	{
		PG_RETURN_NULL();
	}
	pgbsonelement inputPgbsonElement;
	PgbsonToSinglePgbsonElement(inputPgbson, &inputPgbsonElement);
	if (BsonValueIsNumber(&inputPgbsonElement.bsonValue) && !IsBsonValueNaN(
			&inputPgbsonElement.bsonValue))
	{
		/* doesn't throw error if data exceeds the range of double */
		/* take overflow and underflow as infinity */
		double value = BsonValueAsDoubleQuiet(&inputPgbsonElement.bsonValue);
		tdigest_add(state, value);
	}

	PG_RETURN_POINTER(state);
}


/*
 * Compute percentile from a tdigest. Final function for tdigest aggregate
 * with a single percentile.
 */
Datum
tdigest_percentile(PG_FUNCTION_ARGS)
{
	tdigest_aggstate_t *state;
	MemoryContext aggcontext;
	double result;

	/* cannot be called directly because of internal-type argument */
	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		elog(ERROR, "tdigest_percentile called in non-aggregate context");
	}

	/* Return NULL when no digest is present */
	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}

	state = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;

	/* return null bson if no numeric data was added */
	if (state->count == 0)
	{
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}

	tdigest_compute_quantiles(state, &result);

	finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
	finalValue.bsonValue.value.v_double = result;

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Compute percentiles from a tdigest. Final function for tdigest aggregate
 * with an array of percentiles.
 */
Datum
tdigest_array_percentiles(PG_FUNCTION_ARGS)
{
	double *result;
	MemoryContext aggcontext;

	tdigest_aggstate_t *state;

	/* cannot be called directly because of internal-type argument */
	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		elog(ERROR, "tdigest_array_percentiles called in non-aggregate context");
	}

	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}

	state = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);

	/* return null bson if no numeric data was added */
	if (state->count == 0)
	{
		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
		for (int i = 0; i < state->npercentiles; i++)
		{
			bson_value_t val = {
				.value_type = BSON_TYPE_NULL
			};
			PgbsonArrayWriterWriteValue(&arrayWriter, &val);
		}

		PgbsonWriterEndArray(&writer, &arrayWriter);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}

	result = palloc0(state->npercentiles * sizeof(double));

	tdigest_compute_quantiles(state, result);

	return double_array_to_bson_array(fcinfo, result, state->npercentiles);
}


Datum
tdigest_serial(PG_FUNCTION_ARGS)
{
	bytea *v;
	tdigest_aggstate_t *state;
	Size len;
	char *ptr;

	state = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);

	len = offsetof(tdigest_aggstate_t, percentiles) +
		  state->npercentiles * sizeof(double) +
		  state->nvalues * sizeof(double) +
		  state->ncentroids * sizeof(centroid_t);

	v = palloc(len + VARHDRSZ);

	SET_VARSIZE(v, len + VARHDRSZ);
	ptr = VARDATA(v);

	memcpy(ptr, state, offsetof(tdigest_aggstate_t, percentiles));
	ptr += offsetof(tdigest_aggstate_t, percentiles);

	if (state->npercentiles > 0)
	{
		memcpy(ptr, state->percentiles, sizeof(double) * state->npercentiles);
		ptr += sizeof(double) * state->npercentiles;
	}

	if (state->nvalues > 0)
	{
		memcpy(ptr, state->values, sizeof(double) * state->nvalues);
		ptr += sizeof(double) * state->nvalues;
	}

	/* FIXME maybe don't serialize full centroids, but just sum/count */
	memcpy(ptr, state->centroids,
		   sizeof(centroid_t) * state->ncentroids);
	ptr += sizeof(centroid_t) * state->ncentroids;

	Assert(VARDATA(v) + len == ptr);

	PG_RETURN_POINTER(v);
}


Datum
tdigest_deserial(PG_FUNCTION_ARGS)
{
	bytea *v = (bytea *) PG_GETARG_POINTER(0);
	char *ptr = VARDATA_ANY(v);
	tdigest_aggstate_t tmp;
	tdigest_aggstate_t *state;
	double *percentiles = NULL;
	double *values = NULL;

	/* copy aggstate header into a local variable */
	memcpy(&tmp, ptr, offsetof(tdigest_aggstate_t, percentiles));
	ptr += offsetof(tdigest_aggstate_t, percentiles);

	/* allocate and copy percentiles */
	if (tmp.npercentiles > 0)
	{
		percentiles = palloc(tmp.npercentiles * sizeof(double));
		memcpy(percentiles, ptr, tmp.npercentiles * sizeof(double));
		ptr += tmp.npercentiles * sizeof(double);
	}

	/* allocate and copy values */
	if (tmp.nvalues > 0)
	{
		values = palloc(tmp.nvalues * sizeof(double));
		memcpy(values, ptr, tmp.nvalues * sizeof(double));
		ptr += tmp.nvalues * sizeof(double);
	}

	state = tdigest_aggstate_allocate(tmp.npercentiles, tmp.nvalues,
									  tmp.compression);

	if (tmp.npercentiles > 0)
	{
		memcpy(state->percentiles, percentiles, tmp.npercentiles * sizeof(double));
		pfree(percentiles);
	}

	if (tmp.nvalues > 0)
	{
		memcpy(state->values, values, tmp.nvalues * sizeof(double));
		pfree(values);
	}

	/* copy the data into the newly-allocated state */
	memcpy(state, &tmp, offsetof(tdigest_aggstate_t, percentiles));

	/* we don't need to move the pointer */

	/* copy the centroids back */
	memcpy(state->centroids, ptr,
		   sizeof(centroid_t) * state->ncentroids);
	ptr += sizeof(centroid_t) * state->ncentroids;

	PG_RETURN_POINTER(state);
}


static tdigest_aggstate_t *
tdigest_copy(tdigest_aggstate_t *state)
{
	tdigest_aggstate_t *copy;

	copy = tdigest_aggstate_allocate(state->npercentiles, state->nvalues,
									 state->compression);

	memcpy(copy, state, offsetof(tdigest_aggstate_t, percentiles));

	if (state->nvalues > 0)
	{
		memcpy(copy->values, state->values,
			   sizeof(double) * state->nvalues);
	}

	if (state->npercentiles > 0)
	{
		memcpy(copy->percentiles, state->percentiles,
			   sizeof(double) * state->npercentiles);
	}

	memcpy(copy->centroids, state->centroids,
		   state->ncentroids * sizeof(centroid_t));

	return copy;
}


Datum
tdigest_combine(PG_FUNCTION_ARGS)
{
	tdigest_aggstate_t *src;
	tdigest_aggstate_t *dst;
	MemoryContext aggcontext;
	MemoryContext oldcontext;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		elog(ERROR, "tdigest_combine called in non-aggregate context");
	}

	/* if no "merged" state yet, try creating it */
	if (PG_ARGISNULL(0))
	{
		/* nope, the second argument is NULL to, so return NULL */
		if (PG_ARGISNULL(1))
		{
			PG_RETURN_NULL();
		}

		/* the second argument is not NULL, so copy it */
		src = (tdigest_aggstate_t *) PG_GETARG_POINTER(1);

		/* copy the digest into the right long-lived memory context */
		oldcontext = MemoryContextSwitchTo(aggcontext);
		src = tdigest_copy(src);
		MemoryContextSwitchTo(oldcontext);

		PG_RETURN_POINTER(src);
	}

	/*
	 * If the second argument is NULL, just return the first one (we know
	 * it's not NULL at this point).
	 */
	if (PG_ARGISNULL(1))
	{
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	/* Now we know neither argument is NULL, so merge them. */
	src = (tdigest_aggstate_t *) PG_GETARG_POINTER(1);
	dst = (tdigest_aggstate_t *) PG_GETARG_POINTER(0);

	/*
	 * Do a compaction on each digest, to make sure we have enough space.
	 *
	 * XXX Maybe do this only when necessary, i.e. when we can't fit the
	 * data into the dst digest? Also, is it really ensured this gives us
	 * enough free space?
	 */
	tdigest_compact(dst);
	tdigest_compact(src);

	/* copy the second part */
	memcpy(&dst->centroids[dst->ncentroids],
		   src->centroids,
		   src->ncentroids * sizeof(centroid_t));

	dst->ncentroids += src->ncentroids;
	dst->count += src->count;

	/* mark the digest as not compacted */
	dst->ncompacted = 0;

	PG_RETURN_POINTER(dst);
}


/*
 * Comparator, ordering the centroids by mean value.
 *
 * When the mean is the same, we try ordering the centroids by count.
 *
 * In principle, centroids with the same mean represent the same value,
 * but we still need to care about the count to allow rebalancing the
 * centroids later.
 */
static int
centroid_cmp(const void *a, const void *b)
{
	double ma, mb;

	centroid_t *ca = (centroid_t *) a;
	centroid_t *cb = (centroid_t *) b;

	ma = ca->mean;
	mb = cb->mean;

	if (ma < mb)
	{
		return -1;
	}
	else if (ma > mb)
	{
		return 1;
	}

	if (ca->count < cb->count)
	{
		return -1;
	}
	else if (ca->count > cb->count)
	{
		return 1;
	}

	return 0;
}


/*
 * Transform an input bson to a plain double C array.
 */
static double *
bson_to_double_array(FunctionCallInfo fcinfo, bson_value_t *barray, int *len)
{
	if (barray->value_type != BSON_TYPE_ARRAY || BsonDocumentValueCountKeys(barray) == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION7750301), errmsg(
							"Expected an array containing numbers from 0.0 to 1.0, but instead received: %s",
							BsonValueToJsonForLogging(barray))));
	}

	(*len) = BsonDocumentValueCountKeys(barray);
	double *result = (double *) palloc((*len) * sizeof(double));
	bson_iter_t iter;
	BsonValueInitIterator(barray, &iter);
	int i = 0;
	while (bson_iter_next(&iter))
	{
		const bson_value_t *value = bson_iter_value(&iter);
		if (!BsonValueIsNumber(value))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Accumulator $percentile is not implemented yet")));
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION7750302), errmsg(
								"Expected an array containing numbers from 0.0 to 1.0, but instead received: %s",
								BsonValueToJsonForLogging(value))));
		}
		double percentile = BsonValueAsDoubleQuiet(bson_iter_value(&iter));
		if (percentile < 0 || percentile > 1)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION7750303), errmsg(
								"Expected an array containing numbers from 0.0 to 1.0, but instead received: %lf",
								percentile)));
		}
		result[i] = percentile;
		i++;
	}

	return result;
}


/*
 * construct a bson array from a simple C double array
 */
static Datum
double_array_to_bson_array(FunctionCallInfo fcinfo, double *darray, int len)
{
	bson_value_t val = {
		.value_type = BSON_TYPE_DOUBLE
	};
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
	for (int i = 0; i < len; i++)
	{
		val.value.v_double = darray[i];
		PgbsonArrayWriterWriteValue(&arrayWriter, &val);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	pfree(darray);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}

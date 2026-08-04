#ifndef ODYSSEY_TDIGEST_H
#define ODYSSEY_TDIGEST_H

////////////////////////////////////////////////////////////////////////////////
// tdigest
//
// Copyright (c) 2018 Andrew Werner, All rights reserved.
//
// tdigest is an implementation of Ted Dunning's streaming quantile estimation
// data structure.
// This implementation is intended to be like the new MergingHistogram.
// It focuses on being in portable C that should be easy to integrate into other
// languages. In particular it provides mechanisms to preallocate all memory
// at construction time.
//
// The implementation is a direct descendent of
//  https://github.com/tdunning/t-digest/
//
// TODO: add a Ted Dunning Copyright notice.
//
////////////////////////////////////////////////////////////////////////////////

typedef struct td_histogram td_histogram_t;

// td_new allocates a new histogram.
// It is similar to init but assumes that it can use malloc.
td_histogram_t *td_new(double compression);

// clear histogram in-place
void td_safe_free(td_histogram_t *h);

// copy histogram to another memory
void td_copy(td_histogram_t *dst, td_histogram_t *src);

// td_free frees the memory associated with h.
void td_free(td_histogram_t *h);

// td_add adds val to h with the specified count.
void td_add(td_histogram_t *h, double val, double count);

// td_merge merges the data from from into into.
void td_merge(td_histogram_t *into, td_histogram_t *from);

// td_reset resets a histogram.
void td_reset(td_histogram_t *h);

// td_value_at queries h for the value at q.
// If q is not in [0, 1], NAN will be returned.
double td_value_at(td_histogram_t *h, double q);

// td_value_at queries h for the quantile of val.
// The returned value will be in [0, 1].
double td_quantile_of(td_histogram_t *h, double val);

// td_trimmed_mean returns the mean of data from the lo quantile to the
// hi quantile.
double td_trimmed_mean(td_histogram_t *h, double lo, double hi);

// td_total_count returns the total count contained in h.
double td_total_count(td_histogram_t *h);

// td_total_sum returns the sum of all the data added to h.
double td_total_sum(td_histogram_t *h);

// td_decay multiplies all countes by factor.
void td_decay(td_histogram_t *h, double factor);

#endif /* ODYSSEY_TDIGEST_H */

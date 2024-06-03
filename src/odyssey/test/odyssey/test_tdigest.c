
#include "lrand48.h"
#include <pg_rand48.c>
#include "odyssey.h"

void simple_test()
{
	td_histogram_t *histogram = td_new(100);
	td_add(histogram, 1, 1);
	td_add(histogram, 2, 1);
	assert(td_value_at(histogram, 0) == 1);
	assert(td_value_at(histogram, .5) == 1.5);
	assert(td_value_at(histogram, 1) == 2);
	td_histogram_t *h2 = td_new(100);
	td_add(h2, 0, 1);
	td_add(h2, 3, 1);
	td_merge(histogram, h2);
	assert(td_value_at(histogram, 0) == 0);
	assert(td_value_at(histogram, .5) == 1.5);
	assert(td_value_at(histogram, 1) == 3);
	td_free(h2);
	td_free(histogram);
}

void monotonicity_test()
{
	td_histogram_t *histogram = td_new(100);
	unsigned short xseed[3] = { 123, 42, 21 };
	for (size_t i = 0; i < 100000; ++i) {
		td_add(histogram, pg_erand48(xseed), 1);
	}
	double last_quantile = -1;
	double last_x = -1;
	for (double i = 0; i <= 1; i += 1e-5) {
		double current_x = td_value_at(histogram, i);
		assert(current_x >= last_x);
		last_x = current_x;

		double current_quantile = td_quantile_of(histogram, i);
		assert(current_quantile >= last_quantile);
	}

	td_safe_free(histogram);
}

void extreme_quantiles_test()
{
	td_histogram_t *histogram = td_new(100);
	td_add(histogram, 10, 3);
	td_add(histogram, 20, 1);
	td_add(histogram, 40, 5);
#define size 9
	double expected[size] = { 5., 10., 15., 20., 30., 35., 40., 45., 50. };
	double quantiles[3] = { 1.5 / size, 3.5 / size, 6.5 / size };
	for (size_t i = 0; i < 3; ++i) {
		double quantile = quantiles[i];
		size_t index = floor(quantile * size);
		assert(fabs(td_value_at(histogram, quantile) -
			    expected[index]) < 0.01);
	}
	td_safe_free(histogram);
}

void three_point_test()
{
	td_histogram_t *histogram = td_new(100);
	double x0 = 0.18615591526031494;
	double x1 = 0.4241943657398224;
	double x2 = 0.8813006281852722;

	td_add(histogram, x0, 1);
	td_add(histogram, x1, 1);
	td_add(histogram, x2, 1);

	double p10 = td_value_at(histogram, 0.1);
	double p50 = td_value_at(histogram, 0.5);
	double p90 = td_value_at(histogram, 0.9);
	double p95 = td_value_at(histogram, 0.95);
	double p99 = td_value_at(histogram, 0.99);

	assert(p10 <= p50);
	assert(p50 <= p90);
	assert(p90 <= p95);
	assert(p95 <= p99);

	assert(x0 == p10);
	assert(x2 == p99);

	td_safe_free(histogram);
}

void merge_several_digests_test()
{
	td_histogram_t *hists[5];
	for (size_t i = 0; i < 5; ++i) {
		hists[i] = td_new(100);
	}

	for (size_t i = 0; i < 1000; ++i) {
		td_add(hists[0], i, 1);
	}
	td_histogram_t *common_hist = td_new(100);

	for (size_t i = 0; i < 5; ++i) {
		td_merge(common_hist, hists[i]);
	}

	double quantiles[3] = { 0.5, 0.9, 0.99 };
	for (size_t i = 0; i < 3; ++i) {
		assert(fabs(td_value_at(common_hist, quantiles[i]) -
			    td_value_at(hists[0], quantiles[i])) < 1e-6);
	}

	for (size_t i = 0; i < 5; ++i)
		td_safe_free(hists[i]);
	td_safe_free(common_hist);
}

void tdigest_forward_test();

void tdigest_backward_test();

int tdigest_random_test();

void machinarium_test_tdigest(void)
{
	machinarium_init();
	mm_lrand48_seed();

	tdigest_forward_test();
	tdigest_backward_test();

	int fails = 0;
	for (int i = 0; i < 20; i++)
		fails += tdigest_random_test();

	// fails approx 1/1000, so suppress flaps to impossible
	assert(fails <= 3);

	machinarium_free();
}

int tdigest_random_test()
{
	td_histogram_t *histogram = td_new(100);
	td_histogram_t *freeze = td_new(100);

	for (int i = 0; i < 100000; i++) {
		td_add(histogram, machine_lrand48() % 10000, 1);
	}

	td_copy(freeze, histogram);

	int result = 0;
	if ((int)round(td_value_at(freeze, 0.8) / 2000.0) != 4)
		result++;
	if ((int)round(td_value_at(freeze, 0.6) / 2000.0) != 3)
		result++;
	if ((int)round(td_value_at(freeze, 0.4) / 2000.0) != 2)
		result++;

	td_safe_free(histogram);
	td_safe_free(freeze);

	return result;
}

void tdigest_backward_test()
{
	td_histogram_t *histogram = td_new(100);
	td_histogram_t *freeze = td_new(100);

	for (int i = 1; i <= 100; i++) {
		td_add(histogram, 100 - i, 1);
	}
	td_copy(freeze, histogram);

	assert(fabs(td_value_at(freeze, 0.7) - 70) < 1);
	assert(fabs(td_value_at(freeze, 0.5) - 50) < 1);
	assert(fabs(td_value_at(freeze, 0.3) - 30) < 1);
	td_free(histogram);
	td_free(freeze);
}

void tdigest_forward_test()
{
	td_histogram_t *histogram = td_new(100);
	td_histogram_t *freeze = td_new(100);

	for (int i = 1; i <= 100; i++) {
		td_add(histogram, i, 1);
	}
	td_copy(freeze, histogram);
	assert(fabs(td_value_at(freeze, 0.7) - 70) < 1);
	assert(fabs(td_value_at(freeze, 0.5) - 50) < 1);
	assert(fabs(td_value_at(freeze, 0.3) - 30) < 1);
	td_free(histogram);
	td_free(freeze);
}

void odyssey_test_tdigest(void)
{
	simple_test();
	monotonicity_test();
	extreme_quantiles_test();
	three_point_test();
	merge_several_digests_test();
	machinarium_test_tdigest();
}

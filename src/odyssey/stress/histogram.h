#ifndef OD_HISTOGRAM_H
#define OD_HISTOGRAM_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_HISTOGRAM_COUNT 155

typedef struct od_histogram od_histogram_t;

struct od_histogram {
	int min;
	int max;
	int total;
	size_t buckets[OD_HISTOGRAM_COUNT];
	int count;
};

static const double od_histogram_buckets[OD_HISTOGRAM_COUNT] = { 1,
								 2,
								 3,
								 4,
								 5,
								 6,
								 7,
								 8,
								 9,
								 10,
								 12,
								 14,
								 16,
								 18,
								 20,
								 25,
								 30,
								 35,
								 40,
								 45,
								 50,
								 60,
								 70,
								 80,
								 90,
								 100,
								 120,
								 140,
								 160,
								 180,
								 200,
								 250,
								 300,
								 350,
								 400,
								 450,
								 500,
								 600,
								 700,
								 800,
								 900,
								 1000,
								 1200,
								 1400,
								 1600,
								 1800,
								 2000,
								 2500,
								 3000,
								 3500,
								 4000,
								 4500,
								 5000,
								 6000,
								 7000,
								 8000,
								 9000,
								 10000,
								 12000,
								 14000,
								 16000,
								 18000,
								 20000,
								 25000,
								 30000,
								 35000,
								 40000,
								 45000,
								 50000,
								 60000,
								 70000,
								 80000,
								 90000,
								 100000,
								 120000,
								 140000,
								 160000,
								 180000,
								 200000,
								 250000,
								 300000,
								 350000,
								 400000,
								 450000,
								 500000,
								 600000,
								 700000,
								 800000,
								 900000,
								 1000000,
								 1200000,
								 1400000,
								 1600000,
								 1800000,
								 2000000,
								 2500000,
								 3000000,
								 3500000,
								 4000000,
								 4500000,
								 5000000,
								 6000000,
								 7000000,
								 8000000,
								 9000000,
								 10000000,
								 12000000,
								 14000000,
								 16000000,
								 18000000,
								 20000000,
								 25000000,
								 30000000,
								 35000000,
								 40000000,
								 45000000,
								 50000000,
								 60000000,
								 70000000,
								 80000000,
								 90000000,
								 100000000,
								 120000000,
								 140000000,
								 160000000,
								 180000000,
								 200000000,
								 250000000,
								 300000000,
								 350000000,
								 400000000,
								 450000000,
								 500000000,
								 600000000,
								 700000000,
								 800000000,
								 900000000,
								 1000000000,
								 1200000000,
								 1400000000,
								 1600000000,
								 1800000000,
								 2000000000,
								 2500000000.0,
								 3000000000.0,
								 3500000000.0,
								 4000000000.0,
								 4500000000.0,
								 5000000000.0,
								 6000000000.0,
								 7000000000.0,
								 8000000000.0,
								 9000000000.0,
								 1e200,
								 INFINITY };

static inline void od_histogram_init(od_histogram_t *h)
{
	memset(h, 0, sizeof(*h));
}

static inline void od_histogram_add(od_histogram_t *h, int time_us)
{
	size_t begin = 0;
	size_t end = OD_HISTOGRAM_COUNT - 1;
	size_t mid;
	while (1) {
		mid = begin / 2 + end / 2;
		if (mid == begin) {
			for (mid = end; mid > begin; mid--) {
				if (od_histogram_buckets[mid - 1] < time_us)
					break;
			}
			break;
		}
		if (od_histogram_buckets[mid - 1] < time_us)
			begin = mid;
		else
			end = mid;
	}
	if (h->min > time_us)
		h->min = time_us;
	if (h->max < time_us)
		h->max = time_us;
	h->total += time_us;
	h->buckets[mid]++;
	h->count++;
}

static inline void od_histogram_print(od_histogram_t *h, int clients,
				      int run_time_sec)
{
	if (h->count == 0)
		return;
	printf("\n");
	printf("[%7s, %7s]\t%11s\t%7s\n", "min us", "max us", "count", "%");
	printf("--------------------------------------------------\n");
	int i = 0;
	for (; i < OD_HISTOGRAM_COUNT; i++) {
		if (h->buckets[i] == 0.0)
			continue;
		double percents = (double)h->buckets[i] / h->count;
		printf("[%7.0lf, %7.0lf]\t%11zu\t%7.2lf\n",
		       (i > 0) ? od_histogram_buckets[i - 1] : 0.0,
		       od_histogram_buckets[i], h->buckets[i], percents * 1e2);
	}

	double avg_latency = h->total / (double)h->count;
	printf("--------------------------------------------------\n");
	printf("total:%5s%7.0d\t%11d\t   100%%\n", "", h->total, h->count);
	printf("\n");
	printf("min latency       : %d usec/op\n", h->min);
	printf("avg latency       : %.2f usec/op\n", avg_latency);
	printf("max latency       : %d usec/op\n", h->max);

	printf("throughput (real) : %.2f ops/sec\n",
	       (double)h->count / (run_time_sec));

	printf("throughput        : %.2f ops/sec\n",
	       (double)h->count / ((double)h->total / clients / 1000000.0));
}

static inline int od_histogram_time_us(void)
{
	return machine_time_us();
}

#endif /* OD_HISTOGRAM_H */

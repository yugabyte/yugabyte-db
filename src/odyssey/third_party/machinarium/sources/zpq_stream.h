
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#ifndef MM_ZPQ_STREAM_H
#define MM_ZPQ_STREAM_H

#include <stdlib.h>

#define MM_ZPQ_IO_ERROR (-1)
#define MM_ZPQ_DECOMPRESS_ERROR (-2)
#define MM_ZPQ_MAX_ALGORITHMS (8)
#define MM_ZPQ_NO_COMPRESSION 'n'

struct mm_zpq_stream;
typedef struct mm_zpq_stream mm_zpq_stream_t;

typedef ssize_t (*mm_zpq_tx_func)(void *arg, void const *data, size_t size);
typedef ssize_t (*mm_zpq_rx_func)(void *arg, void *data, size_t size);

mm_zpq_stream_t *zpq_create(int impl, mm_zpq_tx_func tx_func,
			    mm_zpq_rx_func rx_func, void *arg, char *rx_data,
			    size_t rx_data_size);
ssize_t mm_zpq_read(mm_zpq_stream_t *zs, void *buf, size_t size);
ssize_t mm_zpq_write(mm_zpq_stream_t *zs, void const *buf, size_t size,
		     size_t *processed);
char const *mm_zpq_error(mm_zpq_stream_t *zs);
size_t mm_zpq_buffered_tx(mm_zpq_stream_t *zs);
size_t mm_zpq_buffered_rx(mm_zpq_stream_t *zs);
_Bool mm_zpq_deferred_rx(mm_zpq_stream_t *zs);
void mm_zpq_free(mm_zpq_stream_t *zs);

void mm_zpq_get_supported_algorithms(char *algorithms);
int mm_zpq_get_algorithm_impl(char name);

#endif

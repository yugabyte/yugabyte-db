
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include "zpq_stream.h"
#include <assert.h>
#include <string.h>

/*
 * Functions implementing streaming compression algorithm
 */
typedef struct {
	/*
	 * Returns letter identifying compression algorithm.
	 */
	char (*name)(void);

	/*
	 * Create compression stream with using rx/tx function for fetching/sending
	 * compressed data. tx_func: function for writing compressed data in
	 * underlying stream rx_func: function for receiving compressed data from
	 * underlying stream arg: context passed to the function rx_data: received
	 * data (compressed data already fetched from input stream) rx_data_size:
	 * size of data fetched from input stream
	 */
	mm_zpq_stream_t *(*create)(mm_zpq_tx_func tx_func,
				   mm_zpq_rx_func rx_func, void *arg,
				   char *rx_data, size_t rx_data_size);

	/*
	 * Read up to "size" raw (decompressed) bytes.
	 * Returns number of decompressed bytes or error code.
	 * Error code is either ZPQ_DECOMPRESS_ERROR either error code returned by
	 * the rx function.
	 */
	ssize_t (*read)(mm_zpq_stream_t *zs, void *buf, size_t size);

	/*
	 * Write up to "size" raw (decompressed) bytes.
	 * Returns number of written raw bytes or error code returned by tx
	 * function. In the last case amount of written raw bytes is stored in
	 * *processed.
	 */
	ssize_t (*write)(mm_zpq_stream_t *zs, void const *buf, size_t size,
			 size_t *processed);

	/*
	 * Free stream created by create function.
	 */
	void (*free)(mm_zpq_stream_t *zs);

	/*
	 * Get error message.
	 */
	char const *(*error)(mm_zpq_stream_t *zs);

	/*
	 * Returns estimated amount of data left in internal tx decompression
	 * buffer.
	 */
	size_t (*buffered_tx)(mm_zpq_stream_t *zs);

	/*
	 * Returns estimated amount of data left in internal rx compression
	 * buffer.
	 */
	size_t (*buffered_rx)(mm_zpq_stream_t *zs);

	/*
	 * Returns 1 if there is deferred rx_func call operation.
	 * Otherwise returns 0.
	 */
	_Bool (*deferred_rx)(mm_zpq_stream_t *zs);
} zpq_algorithm_t;

struct mm_zpq_stream {
	zpq_algorithm_t const *algorithm;
};
#ifdef MM_BUILD_COMPRESSION
#ifdef MM_HAVE_ZSTD

#include <stdlib.h>
#include <zstd.h>

#define MM_ZSTD_BUFFER_SIZE (8 * 1024)
#define MM_ZSTD_COMPRESSION_LEVEL 1

typedef struct zstd_stream {
	mm_zpq_stream_t common;
	ZSTD_CStream *tx_stream;
	ZSTD_DStream *rx_stream;
	ZSTD_outBuffer tx;
	ZSTD_inBuffer rx;
	size_t tx_not_flushed; /* Amount of data in internal zstd buffer */
	size_t tx_buffered; /* Data consumed by zstd_write but not yet sent */
	size_t rx_buffered; /* Data which is needed for ztd_read */
	/* Flag that the last call of zstd_read did not call the rx_func */
	_Bool deferred_rx_call;
	mm_zpq_tx_func tx_func;
	mm_zpq_rx_func rx_func;
	void *arg;
	char const *rx_error; /* Decompress error message */
	size_t tx_total;
	size_t tx_total_raw;
	size_t rx_total;
	size_t rx_total_raw;
	char tx_buf[MM_ZSTD_BUFFER_SIZE];
	char rx_buf[MM_ZSTD_BUFFER_SIZE];
} zstd_stream_t;

static mm_zpq_stream_t *zstd_create(mm_zpq_tx_func tx_func,
				    mm_zpq_rx_func rx_func, void *arg,
				    char *rx_data, size_t rx_data_size)
{
	zstd_stream_t *zs = (zstd_stream_t *)malloc(sizeof(zstd_stream_t));

	zs->tx_stream = ZSTD_createCStream();
	ZSTD_initCStream(zs->tx_stream, MM_ZSTD_COMPRESSION_LEVEL);
	zs->rx_stream = ZSTD_createDStream();
	ZSTD_initDStream(zs->rx_stream);
	zs->tx.dst = zs->tx_buf;
	zs->tx.pos = 0;
	zs->tx.size = MM_ZSTD_BUFFER_SIZE;
	zs->rx.src = zs->rx_buf;
	zs->rx.pos = 0;
	zs->rx.size = 0;
	zs->rx_func = rx_func;
	zs->tx_func = tx_func;
	zs->tx_buffered = 0;
	zs->rx_buffered = 0;
	zs->tx_not_flushed = 0;
	zs->rx_error = NULL;
	zs->arg = arg;
	zs->tx_total = zs->tx_total_raw = 0;
	zs->rx_total = zs->rx_total_raw = 0;
	zs->rx.size = rx_data_size;
	zs->deferred_rx_call = 0;
	assert(rx_data_size < MM_ZSTD_BUFFER_SIZE);
	memcpy(zs->rx_buf, rx_data, rx_data_size);

	return (mm_zpq_stream_t *)zs;
}

static ssize_t zstd_read(mm_zpq_stream_t *zstream, void *buf, size_t size)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	ssize_t rc;
	ZSTD_outBuffer out;
	out.dst = buf;
	out.pos = 0;
	out.size = size;

	while (1) {
		/* store the incomplete rx attempt flag */
		zs->deferred_rx_call = 1;
		if (zs->rx.pos != zs->rx.size || zs->rx_buffered == 0) {
			rc = ZSTD_decompressStream(zs->rx_stream, &out,
						   &zs->rx);
			if (ZSTD_isError(rc)) {
				zs->rx_error = ZSTD_getErrorName(rc);
				return MM_ZPQ_DECOMPRESS_ERROR;
			}
			/* Return result if we fill requested amount of bytes or read
			 * operation was performed */
			if (out.pos != 0) {
				zs->rx_total_raw += out.pos;
				zs->rx_buffered = 0;
				return out.pos;
			}
			zs->rx_buffered = rc;
			if (zs->rx.pos == zs->rx.size) {
				zs->rx.pos = zs->rx.size =
					0; /* Reset rx buffer */
			}
		}
		rc = zs->rx_func(zs->arg, (char *)zs->rx.src + zs->rx.size,
				 MM_ZSTD_BUFFER_SIZE - zs->rx.size);
		/* if we've made a call to rx function, reset the deferred rx flag */
		zs->deferred_rx_call = 0;
		if (rc > 0) /* read fetches some data */
		{
			zs->rx.size += rc;
			zs->rx_total += rc;
		} else /* read failed */
		{
			zs->rx_total_raw += out.pos;
			return rc;
		}
	}
}

static ssize_t zstd_write(mm_zpq_stream_t *zstream, void const *buf,
			  size_t size, size_t *processed)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	ssize_t rc;
	ZSTD_inBuffer in_buf;
	in_buf.src = buf;
	in_buf.pos = 0;
	in_buf.size = size;

	do {
		if (zs->tx.pos == 0) /* Compress buffer is empty */
		{
			zs->tx.dst =
				zs->tx_buf; /* Reset pointer to the beginning of buffer */

			if (in_buf.pos <
			    size) /* Has something to compress in input buffer */
				ZSTD_compressStream(zs->tx_stream, &zs->tx,
						    &in_buf);

			if (in_buf.pos ==
			    size) /* All data is compressed: flushed internal zstd buffer */
			{
				zs->tx_not_flushed = ZSTD_flushStream(
					zs->tx_stream, &zs->tx);
			}
		}
		rc = zs->tx_func(zs->arg, zs->tx.dst, zs->tx.pos);
		if (rc > 0) {
			zs->tx.pos -= rc;
			zs->tx.dst = (char *)zs->tx.dst + rc;
			zs->tx_total += rc;
		} else {
			*processed = in_buf.pos;
			zs->tx_buffered = zs->tx.pos;
			zs->tx_total_raw += in_buf.pos;
			return rc;
		}
		/* repeat sending while there is some data in input or internal zstd
		 * buffer */
	} while (in_buf.pos < size || zs->tx_not_flushed);

	zs->tx_total_raw += in_buf.pos;
	zs->tx_buffered = zs->tx.pos;
	return in_buf.pos;
}

static void zstd_free(mm_zpq_stream_t *zstream)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	if (zs != NULL) {
		ZSTD_freeCStream(zs->tx_stream);
		ZSTD_freeDStream(zs->rx_stream);
		free(zs);
	}
}

static char const *zstd_error(mm_zpq_stream_t *zstream)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	return zs->rx_error;
}

static size_t zstd_buffered_tx(mm_zpq_stream_t *zstream)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	return zs != NULL ? zs->tx_buffered + zs->tx_not_flushed : 0;
}

static size_t zstd_buffered_rx(mm_zpq_stream_t *zstream)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	return zs != NULL ? zs->rx.size - zs->rx.pos : 0;
}

static _Bool zstd_deferred_rx(mm_zpq_stream_t *zstream)
{
	zstd_stream_t *zs = (zstd_stream_t *)zstream;
	return zs != NULL ? zs->deferred_rx_call : 0;
}

static char zstd_name(void)
{
	return 'f';
}

#endif

#ifdef MM_HAVE_ZLIB

#include <stdlib.h>
#include <zlib.h>

#define MM_ZLIB_BUFFER_SIZE \
	8192 /* We have to flush stream after each protocol command        \
			      * and command is mostly limited by record length,            \
			      * which in turn usually less than page size (except TOAST)   \
			      */
#define MM_ZLIB_COMPRESSION_LEVEL \
	1 /* Experiments shows that default (fastest) compression level    \
			   * provides the best size/speed ratio. It is significantly       \
			   * (times) faster than more expensive levels and differences in  \
			   * compression ratio is not so large                             \
			   */

typedef struct zlib_stream {
	mm_zpq_stream_t common;

	z_stream tx;
	z_stream rx;

	mm_zpq_tx_func tx_func;
	mm_zpq_rx_func rx_func;
	void *arg;
	unsigned tx_deflate_pending;
	/* Flag that the last call of zlib_read did not call the rx_func */
	_Bool deferred_rx_call;
	size_t tx_buffered;

	Bytef tx_buf[MM_ZLIB_BUFFER_SIZE];
	Bytef rx_buf[MM_ZLIB_BUFFER_SIZE];
} zlib_stream_t;

static mm_zpq_stream_t *zlib_create(mm_zpq_tx_func tx_func,
				    mm_zpq_rx_func rx_func, void *arg,
				    char *rx_data, size_t rx_data_size)
{
	int rc;
	zlib_stream_t *zs = (zlib_stream_t *)malloc(sizeof(zlib_stream_t));
	memset(&zs->tx, 0, sizeof(zs->tx));
	zs->tx.next_out = zs->tx_buf;
	zs->tx.avail_out = MM_ZLIB_BUFFER_SIZE;
	zs->tx_buffered = 0;
	rc = deflateInit(&zs->tx, MM_ZLIB_COMPRESSION_LEVEL);
	if (rc != Z_OK) {
		free(zs);
		return NULL;
	}
	assert(zs->tx.next_out == zs->tx_buf &&
	       zs->tx.avail_out == MM_ZLIB_BUFFER_SIZE);

	memset(&zs->rx, 0, sizeof(zs->tx));
	zs->rx.next_in = zs->rx_buf;
	zs->rx.avail_in = MM_ZLIB_BUFFER_SIZE;
	zs->tx_deflate_pending = 0;
	zs->deferred_rx_call = 0;
	rc = inflateInit(&zs->rx);
	if (rc != Z_OK) {
		free(zs);
		return NULL;
	}
	assert(zs->rx.next_in == zs->rx_buf &&
	       zs->rx.avail_in == MM_ZLIB_BUFFER_SIZE);

	zs->rx.avail_in = rx_data_size;
	assert(rx_data_size < MM_ZLIB_BUFFER_SIZE);
	memcpy(zs->rx_buf, rx_data, rx_data_size);

	zs->rx_func = rx_func;
	zs->tx_func = tx_func;
	zs->arg = arg;

	return (mm_zpq_stream_t *)zs;
}

static ssize_t zlib_read(mm_zpq_stream_t *zstream, void *buf, size_t size)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	int rc;
	zs->rx.next_out = (Bytef *)buf;
	zs->rx.avail_out = size;

	while (1) {
		/* store the incomplete rx attempt flag */
		zs->deferred_rx_call = 1;
		if (zs->rx.avail_in !=
		    0) /* If there is some data in receiver buffer,
		                             then decompress it */
		{
			rc = inflate(&zs->rx, Z_SYNC_FLUSH);
			if (rc != Z_OK && rc != Z_BUF_ERROR) {
				return MM_ZPQ_DECOMPRESS_ERROR;
			}
			if (zs->rx.avail_out != size) {
				return size - zs->rx.avail_out;
			}
			if (zs->rx.avail_in == 0) {
				zs->rx.next_in = zs->rx_buf;
			}
		} else {
			zs->rx.next_in = zs->rx_buf;
		}
		rc = zs->rx_func(zs->arg, zs->rx.next_in + zs->rx.avail_in,
				 zs->rx_buf + MM_ZLIB_BUFFER_SIZE -
					 zs->rx.next_in - zs->rx.avail_in);
		/* if we've made a call to rx function, reset the deferred rx flag */
		zs->deferred_rx_call = 0;
		if (rc > 0) {
			zs->rx.avail_in += rc;
		} else {
			return rc;
		}
	}
}

static ssize_t zlib_write(mm_zpq_stream_t *zstream, void const *buf,
			  size_t size, size_t *processed)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	int rc;
	zs->tx.next_in = (Bytef *)buf;
	zs->tx.avail_in = size;
	do {
		if (zs->tx.avail_out ==
		    MM_ZLIB_BUFFER_SIZE) /* Compress buffer is empty */
		{
			zs->tx.next_out =
				zs->tx_buf; /* Reset pointer to the  beginning of buffer */

			if (zs->tx.avail_in != 0 ||
			    (zs->tx_deflate_pending >
			     0)) /* Has something in input or deflate buffer */
			{
				rc = deflate(&zs->tx, Z_SYNC_FLUSH);
				assert(rc == Z_OK);
				deflatePending(
					&zs->tx, &zs->tx_deflate_pending,
					Z_NULL); /* check if any data left in deflate buffer */
				zs->tx.next_out =
					zs->tx_buf; /* Reset pointer to the  beginning of buffer */
			}
		}
		rc = zs->tx_func(zs->arg, zs->tx.next_out,
				 MM_ZLIB_BUFFER_SIZE - zs->tx.avail_out);
		if (rc > 0) {
			zs->tx.next_out += rc;
			zs->tx.avail_out += rc;
		} else {
			*processed = size - zs->tx.avail_in;
			zs->tx_buffered =
				MM_ZLIB_BUFFER_SIZE - zs->tx.avail_out;
			return rc;
		}
		/* repeat sending while there is some data in input or deflate buffer */
	} while (zs->tx.avail_in != 0 || zs->tx_deflate_pending > 0);

	zs->tx_buffered = MM_ZLIB_BUFFER_SIZE - zs->tx.avail_out;

	return size - zs->tx.avail_in;
}

static void zlib_free(mm_zpq_stream_t *zstream)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	if (zs != NULL) {
		inflateEnd(&zs->rx);
		deflateEnd(&zs->tx);
		free(zs);
	}
}

static char const *zlib_error(mm_zpq_stream_t *zstream)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	return zs->rx.msg;
}

static size_t zlib_buffered_tx(mm_zpq_stream_t *zstream)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	return zs != NULL ? zs->tx_buffered + zs->tx_deflate_pending : 0;
}

static size_t zlib_buffered_rx(mm_zpq_stream_t *zstream)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	return zs != NULL ? zs->rx.avail_in : 0;
}

static _Bool zlib_deferred_rx(mm_zpq_stream_t *zstream)
{
	zlib_stream_t *zs = (zlib_stream_t *)zstream;
	return zs != NULL ? zs->deferred_rx_call : 0;
}

static char zlib_name(void)
{
	return 'z';
}

#endif
#endif

/*
 * Array with all supported compression algorithms.
 */
static zpq_algorithm_t const zpq_algorithms[] = {

#ifdef MM_BUILD_COMPRESSION
#ifdef MM_HAVE_ZSTD
	{ zstd_name, zstd_create, zstd_read, zstd_write, zstd_free, zstd_error,
	  zstd_buffered_tx, zstd_buffered_rx, zstd_deferred_rx },
#endif
#ifdef MM_HAVE_ZLIB
	{ zlib_name, zlib_create, zlib_read, zlib_write, zlib_free, zlib_error,
	  zlib_buffered_tx, zlib_buffered_rx, zlib_deferred_rx },
#endif
#endif
	{ NULL }
};

/*
 * Index of used compression algorithm in zpq_algorithms array.
 */
mm_zpq_stream_t *zpq_create(int algorithm_impl, mm_zpq_tx_func tx_func,
			    mm_zpq_rx_func rx_func, void *arg, char *rx_data,
			    size_t rx_data_size)
{
	mm_zpq_stream_t *stream = zpq_algorithms[algorithm_impl].create(
		tx_func, rx_func, arg, rx_data, rx_data_size);
	if (stream)
		stream->algorithm = &zpq_algorithms[algorithm_impl];
	return stream;
}

ssize_t mm_zpq_read(mm_zpq_stream_t *zs, void *buf, size_t size)
{
	return zs->algorithm->read(zs, buf, size);
}

ssize_t mm_zpq_write(mm_zpq_stream_t *zs, void const *buf, size_t size,
		     size_t *processed)
{
	return zs->algorithm->write(zs, buf, size, processed);
}

void mm_zpq_free(mm_zpq_stream_t *zs)
{
	if (zs)
		zs->algorithm->free(zs);
}

char const *mm_zpq_error(mm_zpq_stream_t *zs)
{
	return zs->algorithm->error(zs);
}

size_t mm_zpq_buffered_rx(mm_zpq_stream_t *zs)
{
	return zs ? zs->algorithm->buffered_rx(zs) : 0;
}

size_t mm_zpq_buffered_tx(mm_zpq_stream_t *zs)
{
	return zs ? zs->algorithm->buffered_tx(zs) : 0;
}

_Bool mm_zpq_deferred_rx(mm_zpq_stream_t *zs)
{
	return zs ? zs->algorithm->deferred_rx(zs) : 0;
}

/*
 * Get list of the supported algorithms.
 * Each algorithm is identified by one letter: 'f' - Facebook zstd, 'z' - zlib.
 * Algorithm identifies are appended to the provided buffer and terminated by
 * '\0'.
 */
void mm_zpq_get_supported_algorithms(char *algorithms)
{
	int i;
	for (i = 0; zpq_algorithms[i].name != NULL; i++) {
		assert(i < MM_ZPQ_MAX_ALGORITHMS);
		algorithms[i] = zpq_algorithms[i].name();
	}
	assert(i < MM_ZPQ_MAX_ALGORITHMS);
	algorithms[i] = '\0';
}

/*
 * Choose current algorithm implementation.
 * Returns implementation number or -1 if algorithm with such name is not found
 */
int mm_zpq_get_algorithm_impl(char name)
{
	int i;
	if (name != MM_ZPQ_NO_COMPRESSION) {
		for (i = 0; zpq_algorithms[i].name != NULL; i++) {
			if (zpq_algorithms[i].name() == name) {
				return i;
			}
		}
	}
	return -1;
}

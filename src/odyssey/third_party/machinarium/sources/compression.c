/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */
#include <machinarium.h>
#include <machinarium_private.h>

void mm_compression_free(mm_io_t *io)
{
	if (io->zpq_stream)
		mm_zpq_free(io->zpq_stream);
}

int mm_compression_writev(mm_io_t *io, struct iovec *iov, int n,
			  size_t *processed)
{
	int size = mm_iov_size_of(iov, n);
	char *buffer = malloc(size);
	if (buffer == NULL) {
		errno = ENOMEM;
		return -1;
	}
	mm_iovcpy(buffer, iov, n);

	int rc;
	rc = mm_zpq_write(io->zpq_stream, buffer, size, processed);
	free(buffer);
	return rc;
}

/* Returns value > 0 when there is read operation pending. */
int mm_compression_read_pending(mm_io_t *io)
{
	return mm_zpq_buffered_rx(io->zpq_stream) ||
	       mm_zpq_deferred_rx(io->zpq_stream);
}

/* Returns value > 0 when there is write operation pending. */
int mm_compression_write_pending(mm_io_t *io)
{
	return mm_zpq_buffered_tx(io->zpq_stream);
}

/*
 * If client request compression, it sends list of supported
 * compression algorithms - client_compression_algorithms.
 * Each compression algorithm is identified
 * by one letter ('f' - Facebook zstd, 'z' - zlib).
 * Return value is the compression algorithm chosen by intersection
 * of client and server supported compression algorithms.
 * If match is not found, return value is MM_ZPQ_NO_COMPRESSION */
MACHINE_API
char machine_compression_choose_alg(char *client_compression_algorithms)
{
	(void)client_compression_algorithms;
	/* chosen compression algorithm */
	char compression_algorithm = MM_ZPQ_NO_COMPRESSION;
#ifdef MM_BUILD_COMPRESSION
	/* machinarium supported libpq compression algorithms */
	char server_compression_algorithms[MM_ZPQ_MAX_ALGORITHMS];

	/* get list of compression algorithms supported by machinarium */
	mm_zpq_get_supported_algorithms(server_compression_algorithms);

	/* intersect lists */
	while (*client_compression_algorithms != '\0') {
		if (strchr(server_compression_algorithms,
			   *client_compression_algorithms)) {
			compression_algorithm = *client_compression_algorithms;
			break;
		}
		client_compression_algorithms += 1;
	}
#endif
	return compression_algorithm;
}

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */
#ifndef MM_COMPRESSION_H
#define MM_COMPRESSION_H

void mm_compression_free(mm_io_t *io);

static inline int mm_compression_is_active(mm_io_t *io)
{
	return io->zpq_stream != NULL;
}

int mm_compression_writev(mm_io_t *io, struct iovec *iov, int n,
			  size_t *processed);

int mm_compression_read_pending(mm_io_t *io);

int mm_compression_write_pending(mm_io_t *io);

#endif // MM_COMPRESSION_H

#ifndef KIWI_IO_H
#define KIWI_IO_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

static inline int kiwi_read8(char *out, char **pos, uint32_t *size)
{
	if (kiwi_unlikely(*size < sizeof(uint8_t)))
		return -1;
	*out = *pos[0];
	*size -= sizeof(uint8_t);
	*pos += sizeof(uint8_t);
	return 0;
}

static inline int kiwi_read16(uint16_t *out, char **pos, uint32_t *size)
{
	if (kiwi_unlikely(*size < sizeof(uint16_t)))
		return -1;
	unsigned char *ptr = (unsigned char *)*pos;
	*out = ptr[0] << 8 | ptr[1];
	*size -= sizeof(uint16_t);
	*pos += sizeof(uint16_t);
	return 0;
}

static inline int kiwi_read32(uint32_t *out, char **pos, uint32_t *size)
{
	if (kiwi_unlikely(*size < sizeof(uint32_t)))
		return -1;
	unsigned char *ptr = (unsigned char *)*pos;
	*out = ((uint32_t)ptr[0]) << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
	*size -= sizeof(uint32_t);
	*pos += sizeof(uint32_t);
	return 0;
}

static inline int kiwi_readsz(char **pos, uint32_t *size)
{
	char *p = *pos;
	char *end = p + *size;
	while (p < end && *p)
		p++;
	if (kiwi_unlikely(p == end))
		return -1;
	*size -= (uint32_t)(p - *pos) + 1;
	*pos = p + 1;
	return 0;
}

static inline int kiwi_readn(uint32_t n, char **pos, uint32_t *size)
{
	char *end = *pos + *size;
	char *next = *pos + n;
	if (kiwi_unlikely(next > end))
		return -1;
	*size -= (uint32_t)(next - *pos);
	*pos = next;
	return 0;
}

static inline void kiwi_write8to(char *dest, uint8_t value)
{
	*dest = (char)value;
}

static inline void kiwi_write16to(char *dest, uint16_t value)
{
	dest[0] = (value >> 8) & 255;
	dest[1] = value & 255;
}

static inline void kiwi_write32to(char *dest, uint32_t value)
{
	dest[0] = (value >> 24) & 255;
	dest[1] = (value >> 16) & 255;
	dest[2] = (value >> 8) & 255;
	dest[3] = value & 255;
}
KIWI_API static inline void kiwi_header_set_size(kiwi_header_t *header,
						 uint32_t size)
{
	kiwi_write32to((char *)&header->len, size - 1);
}

static inline void kiwi_write8(char **pos, uint8_t value)
{
	kiwi_write8to(*pos, value);
	*pos = *pos + sizeof(value);
}

static inline void kiwi_write16(char **pos, uint16_t value)
{
	kiwi_write16to(*pos, value);
	*pos = *pos + sizeof(value);
}

static inline void kiwi_write32(char **pos, uint32_t value)
{
	kiwi_write32to(*pos, value);
	*pos = *pos + sizeof(value);
}

static inline void kiwi_write(char **pos, char *buf, int size)
{
	memcpy(*pos, buf, size);
	*pos = *pos + size;
}

KIWI_API static inline int kiwi_read(uint32_t *len, char **data, uint32_t *size)
{
	if (*size < sizeof(kiwi_header_t))
		return sizeof(kiwi_header_t) - *size;
	uint32_t pos_size = *size - sizeof(uint8_t);
	char *pos = *data + sizeof(uint8_t);
	/* type */
	kiwi_read32(len, &pos, &pos_size);
	uint32_t len_to_read;
	len_to_read = (*len + sizeof(char)) - *size;
	if (len_to_read > 0)
		return len_to_read;
	*data += sizeof(uint8_t) + *len;
	*size -= sizeof(uint8_t) + *len;
	*len -= sizeof(uint32_t);
	return 0;
}

KIWI_API static inline uint32_t kiwi_read_size(char *data, uint32_t data_size)
{
	assert(data_size >= sizeof(kiwi_header_t));

	/* type */
	uint32_t pos_size = data_size - sizeof(uint8_t);
	char *pos = data + sizeof(uint8_t);

	/* size */
	uint32_t size = 0;
	kiwi_read32(&size, &pos, &pos_size);
	return size;
}

KIWI_API static inline uint32_t kiwi_read_startup_size(char *data,
						       uint32_t data_size)
{
	assert(data_size >= sizeof(uint32_t));
	/* size */
	uint32_t size = 0;
	kiwi_read32(&size, &data, &data_size);
	size -= sizeof(uint32_t);
	return size;
}

#define KIWI_LONG_MESSAGE_SIZE 640 * 1024 * 1024 /* Outght to be enough */

KIWI_API static inline int
kiwi_validate_startup_header(char *data, uint32_t data_size, uint32_t *size)
{
	(void)data_size; // Silent Compiler warnings
	assert(data_size >= sizeof(uint32_t));
	*size = kiwi_read_startup_size(data, sizeof(uint32_t));
	/* do not expect big startup messages */
	if (kiwi_unlikely(*size >= KIWI_LONG_MESSAGE_SIZE))
		return -1;
	return 0;
}

KIWI_API static inline int kiwi_validate_header(char *data, uint32_t data_size,
						uint32_t *size)
{
	(void)data_size; // Silent Compiler warnings
	assert(data_size >= sizeof(kiwi_header_t));
	*size = kiwi_read_size(data, sizeof(kiwi_header_t));

	kiwi_header_t *header = (kiwi_header_t *)data;
	if (kiwi_unlikely(*size < sizeof(uint32_t)))
		return -1;

	/* check supported protocol message type */
	if (kiwi_unlikely(header->type < 0x20))
		return -1;

	/* is small packet */
	if (kiwi_likely(*size < KIWI_LONG_MESSAGE_SIZE))
		return 0;

	/*
	 * Lists the backend and frontend message types that could be "long" (more
	 * than a couple of kilobytes).
	 */
	switch (header->type) {
	/* backend */
	case KIWI_BE_ROW_DESCRIPTION:
	case KIWI_BE_DATA_ROW:
	case KIWI_BE_COPY_DATA:
	case KIWI_BE_FUNCTION_CALL_RESPONSE:
	case KIWI_BE_ERROR_RESPONSE:
	case KIWI_BE_NOTICE_RESPONSE:
	case KIWI_BE_NOTIFICATION_RESPONSE:
	case KIWI_BE_PARAMETER_DESCRIPTION:
	/* frontend */
	case KIWI_FE_BIND:
	case KIWI_FE_PARSE:
	case KIWI_FE_QUERY:
		/* KIWI_FE_COPY_DATA has same type as BE_COPY_DATA */
		return 0;
	}

	return -1;
}

#endif /* KIWI_IO_H */

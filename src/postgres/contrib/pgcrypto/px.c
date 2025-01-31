/*
 * px.c
 *		Various cryptographic stuff for PostgreSQL.
 *
 * Copyright (c) 2001 Marko Kreen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * contrib/pgcrypto/px.c
 */

#include "postgres.h"

#include "px.h"

struct error_desc
{
	int			err;
	const char *desc;
};

static const struct error_desc px_err_list[] = {
	{PXE_OK, "Everything ok"},
	{PXE_NO_HASH, "No such hash algorithm"},
	{PXE_NO_CIPHER, "No such cipher algorithm"},
	{PXE_BAD_OPTION, "Unknown option"},
	{PXE_BAD_FORMAT, "Badly formatted type"},
	{PXE_KEY_TOO_BIG, "Key was too big"},
	{PXE_CIPHER_INIT, "Cipher cannot be initialized"},
	{PXE_HASH_UNUSABLE_FOR_HMAC, "This hash algorithm is unusable for HMAC"},
	{PXE_BUG, "pgcrypto bug"},
	{PXE_ARGUMENT_ERROR, "Illegal argument to function"},
	{PXE_UNKNOWN_SALT_ALGO, "Unknown salt algorithm"},
	{PXE_BAD_SALT_ROUNDS, "Incorrect number of rounds"},
	{PXE_NO_RANDOM, "Failed to generate strong random bits"},
	{PXE_DECRYPT_FAILED, "Decryption failed"},
	{PXE_ENCRYPT_FAILED, "Encryption failed"},
	{PXE_PGP_CORRUPT_DATA, "Wrong key or corrupt data"},
	{PXE_PGP_CORRUPT_ARMOR, "Corrupt ascii-armor"},
	{PXE_PGP_UNSUPPORTED_COMPR, "Unsupported compression algorithm"},
	{PXE_PGP_UNSUPPORTED_CIPHER, "Unsupported cipher algorithm"},
	{PXE_PGP_UNSUPPORTED_HASH, "Unsupported digest algorithm"},
	{PXE_PGP_COMPRESSION_ERROR, "Compression error"},
	{PXE_PGP_NOT_TEXT, "Not text data"},
	{PXE_PGP_UNEXPECTED_PKT, "Unexpected packet in key data"},
	{PXE_PGP_MATH_FAILED, "Math operation failed"},
	{PXE_PGP_SHORT_ELGAMAL_KEY, "Elgamal keys must be at least 1024 bits long"},
	{PXE_PGP_UNKNOWN_PUBALGO, "Unknown public-key encryption algorithm"},
	{PXE_PGP_WRONG_KEY, "Wrong key"},
	{PXE_PGP_MULTIPLE_KEYS,
	"Several keys given - pgcrypto does not handle keyring"},
	{PXE_PGP_EXPECT_PUBLIC_KEY, "Refusing to encrypt with secret key"},
	{PXE_PGP_EXPECT_SECRET_KEY, "Cannot decrypt with public key"},
	{PXE_PGP_NOT_V4_KEYPKT, "Only V4 key packets are supported"},
	{PXE_PGP_KEYPKT_CORRUPT, "Corrupt key packet"},
	{PXE_PGP_NO_USABLE_KEY, "No encryption key found"},
	{PXE_PGP_NEED_SECRET_PSW, "Need password for secret key"},
	{PXE_PGP_BAD_S2K_MODE, "Bad S2K mode"},
	{PXE_PGP_UNSUPPORTED_PUBALGO, "Unsupported public key algorithm"},
	{PXE_PGP_MULTIPLE_SUBKEYS, "Several subkeys not supported"},

	{0, NULL},
};

/*
 * Call ereport(ERROR, ...), with an error code and message corresponding to
 * the PXE_* error code given as argument.
 *
 * This is similar to px_strerror(err), but for some errors, we fill in the
 * error code and detail fields more appropriately.
 */
void
px_THROW_ERROR(int err)
{
	if (err == PXE_NO_RANDOM)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not generate a random number")));
	}
	else
	{
		/* For other errors, use the message from the above list. */
		ereport(ERROR,
				(errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
				 errmsg("%s", px_strerror(err))));
	}
}

const char *
px_strerror(int err)
{
	const struct error_desc *e;

	for (e = px_err_list; e->desc; e++)
		if (e->err == err)
			return e->desc;
	return "Bad error code";
}

/* memset that must not be optimized away */
void
px_memset(void *ptr, int c, size_t len)
{
	memset(ptr, c, len);
}

const char *
px_resolve_alias(const PX_Alias *list, const char *name)
{
	while (list->name)
	{
		if (pg_strcasecmp(list->alias, name) == 0)
			return list->name;
		list++;
	}
	return name;
}

static void (*debug_handler) (const char *) = NULL;

void
px_set_debug_handler(void (*handler) (const char *))
{
	debug_handler = handler;
}

void
px_debug(const char *fmt,...)
{
	va_list		ap;

	va_start(ap, fmt);
	if (debug_handler)
	{
		char		buf[512];

		vsnprintf(buf, sizeof(buf), fmt, ap);
		debug_handler(buf);
	}
	va_end(ap);
}

/*
 * combo - cipher + padding (+ checksum)
 */

static unsigned
combo_encrypt_len(PX_Combo *cx, unsigned dlen)
{
	return dlen + 512;
}

static unsigned
combo_decrypt_len(PX_Combo *cx, unsigned dlen)
{
	return dlen;
}

static int
combo_init(PX_Combo *cx, const uint8 *key, unsigned klen,
		   const uint8 *iv, unsigned ivlen)
{
	int			err;
	unsigned	ks,
				ivs;
	PX_Cipher  *c = cx->cipher;
	uint8	   *ivbuf = NULL;
	uint8	   *keybuf;

	ks = px_cipher_key_size(c);

	ivs = px_cipher_iv_size(c);
	if (ivs > 0)
	{
		ivbuf = palloc0(ivs);
		if (ivlen > ivs)
			memcpy(ivbuf, iv, ivs);
		else if (ivlen > 0)
			memcpy(ivbuf, iv, ivlen);
	}

	if (klen > ks)
		klen = ks;
	keybuf = palloc0(ks);
	memset(keybuf, 0, ks);
	memcpy(keybuf, key, klen);

	err = px_cipher_init(c, keybuf, klen, ivbuf);

	if (ivbuf)
		pfree(ivbuf);
	pfree(keybuf);

	return err;
}

static int
combo_encrypt(PX_Combo *cx, const uint8 *data, unsigned dlen,
			  uint8 *res, unsigned *rlen)
{
	return px_cipher_encrypt(cx->cipher, cx->padding, data, dlen, res, rlen);
}

static int
combo_decrypt(PX_Combo *cx, const uint8 *data, unsigned dlen,
			  uint8 *res, unsigned *rlen)
{
	return px_cipher_decrypt(cx->cipher, cx->padding, data, dlen, res, rlen);
}

static void
combo_free(PX_Combo *cx)
{
	if (cx->cipher)
		px_cipher_free(cx->cipher);
	px_memset(cx, 0, sizeof(*cx));
	pfree(cx);
}

/* PARSER */

static int
parse_cipher_name(char *full, char **cipher, char **pad)
{
	char	   *p,
			   *p2,
			   *q;

	*cipher = full;
	*pad = NULL;

	p = strchr(full, '/');
	if (p != NULL)
		*p++ = 0;
	while (p != NULL)
	{
		if ((q = strchr(p, '/')) != NULL)
			*q++ = 0;

		if (!*p)
		{
			p = q;
			continue;
		}
		p2 = strchr(p, ':');
		if (p2 != NULL)
		{
			*p2++ = 0;
			if (strcmp(p, "pad") == 0)
				*pad = p2;
			else
				return PXE_BAD_OPTION;
		}
		else
			return PXE_BAD_FORMAT;

		p = q;
	}
	return 0;
}

/* provider */

int
px_find_combo(const char *name, PX_Combo **res)
{
	int			err;
	char	   *buf,
			   *s_cipher,
			   *s_pad;

	PX_Combo   *cx;

	cx = palloc0(sizeof(*cx));
	buf = pstrdup(name);

	err = parse_cipher_name(buf, &s_cipher, &s_pad);
	if (err)
	{
		pfree(buf);
		pfree(cx);
		return err;
	}

	err = px_find_cipher(s_cipher, &cx->cipher);
	if (err)
		goto err1;

	if (s_pad != NULL)
	{
		if (strcmp(s_pad, "pkcs") == 0)
			cx->padding = 1;
		else if (strcmp(s_pad, "none") == 0)
			cx->padding = 0;
		else
			goto err1;
	}
	else
		cx->padding = 1;

	cx->init = combo_init;
	cx->encrypt = combo_encrypt;
	cx->decrypt = combo_decrypt;
	cx->encrypt_len = combo_encrypt_len;
	cx->decrypt_len = combo_decrypt_len;
	cx->free = combo_free;

	pfree(buf);

	*res = cx;

	return 0;

err1:
	if (cx->cipher)
		px_cipher_free(cx->cipher);
	pfree(cx);
	pfree(buf);
	return PXE_NO_CIPHER;
}

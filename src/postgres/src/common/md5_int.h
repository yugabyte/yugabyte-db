/*-------------------------------------------------------------------------
 *
 * md5_int.h
 *	  Internal headers for fallback implementation of MD5
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *		  src/common/md5_int.h
 *
 *-------------------------------------------------------------------------
 */

/*	   $KAME: md5.h,v 1.3 2000/02/22 14:01:18 itojun Exp $	   */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *	  may be used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef PG_MD5_INT_H
#define PG_MD5_INT_H

#include "common/md5.h"

#define MD5_BUFLEN 64

/* Context data for MD5 */
typedef struct
{
	union
	{
		uint32		md5_state32[4];
		uint8		md5_state8[16];
	}			md5_st;

#define md5_sta     md5_st.md5_state32[0]
#define md5_stb     md5_st.md5_state32[1]
#define md5_stc     md5_st.md5_state32[2]
#define md5_std     md5_st.md5_state32[3]
#define md5_st8     md5_st.md5_state8

	union
	{
		uint64		md5_count64;
		uint8		md5_count8[8];
	}			md5_count;
#define md5_n   md5_count.md5_count64
#define md5_n8  md5_count.md5_count8

	unsigned int md5_i;
	uint8		md5_buf[MD5_BUFLEN];
} pg_md5_ctx;

/* Interface routines for MD5 */
extern void pg_md5_init(pg_md5_ctx *ctx);
extern void pg_md5_update(pg_md5_ctx *ctx, const uint8 *data, size_t len);
extern void pg_md5_final(pg_md5_ctx *ctx, uint8 *dest);

#endif							/* PG_MD5_INT_H */

/*-------------------------------------------------------------------------
 *
 *	  EUC_JIS_2004, SHIFT_JIS_2004
 *
 * Copyright (c) 2007-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/mb/conversion_procs/euc2004_sjis2004/euc2004_sjis2004.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(euc_jis_2004_to_shift_jis_2004);
PG_FUNCTION_INFO_V1(shift_jis_2004_to_euc_jis_2004);

static int	euc_jis_20042shift_jis_2004(const unsigned char *euc, unsigned char *p, int len, bool noError);
static int	shift_jis_20042euc_jis_2004(const unsigned char *sjis, unsigned char *p, int len, bool noError);

/* ----------
 * conv_proc(
 *		INTEGER,	-- source encoding id
 *		INTEGER,	-- destination encoding id
 *		CSTRING,	-- source string (null terminated C string)
 *		CSTRING,	-- destination string (null terminated C string)
 *		INTEGER,	-- source string length
 *		BOOL		-- if true, don't throw an error if conversion fails
 * ) returns INTEGER;
 *
 * Returns the number of bytes successfully converted.
 * ----------
 */

Datum
euc_jis_2004_to_shift_jis_2004(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_EUC_JIS_2004, PG_SHIFT_JIS_2004);

	converted = euc_jis_20042shift_jis_2004(src, dest, len, noError);

	PG_RETURN_INT32(converted);
}

Datum
shift_jis_2004_to_euc_jis_2004(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_SHIFT_JIS_2004, PG_EUC_JIS_2004);

	converted = shift_jis_20042euc_jis_2004(src, dest, len, noError);

	PG_RETURN_INT32(converted);
}

/*
 * EUC_JIS_2004 -> SHIFT_JIS_2004
 */
static int
euc_jis_20042shift_jis_2004(const unsigned char *euc, unsigned char *p, int len, bool noError)
{
	const unsigned char *start = euc;
	int			c1,
				ku,
				ten;
	int			l;

	while (len > 0)
	{
		c1 = *euc;
		if (!IS_HIGHBIT_SET(c1))
		{
			/* ASCII */
			if (c1 == 0)
			{
				if (noError)
					break;
				report_invalid_encoding(PG_EUC_JIS_2004,
										(const char *) euc, len);
			}
			*p++ = c1;
			euc++;
			len--;
			continue;
		}

		l = pg_encoding_verifymbchar(PG_EUC_JIS_2004, (const char *) euc, len);

		if (l < 0)
		{
			if (noError)
				break;
			report_invalid_encoding(PG_EUC_JIS_2004,
									(const char *) euc, len);
		}

		if (c1 == SS2 && l == 2)	/* JIS X 0201 kana? */
		{
			*p++ = euc[1];
		}
		else if (c1 == SS3 && l == 3)	/* JIS X 0213 plane 2? */
		{
			ku = euc[1] - 0xa0;
			ten = euc[2] - 0xa0;

			switch (ku)
			{
				case 1:
				case 3:
				case 4:
				case 5:
				case 8:
				case 12:
				case 13:
				case 14:
				case 15:
					*p++ = ((ku + 0x1df) >> 1) - (ku >> 3) * 3;
					break;
				default:
					if (ku >= 78 && ku <= 94)
					{
						*p++ = (ku + 0x19b) >> 1;
					}
					else
					{
						if (noError)
							break;
						report_invalid_encoding(PG_EUC_JIS_2004,
												(const char *) euc, len);
					}
			}

			if (ku % 2)
			{
				if (ten >= 1 && ten <= 63)
					*p++ = ten + 0x3f;
				else if (ten >= 64 && ten <= 94)
					*p++ = ten + 0x40;
				else
				{
					if (noError)
						break;
					report_invalid_encoding(PG_EUC_JIS_2004,
											(const char *) euc, len);
				}
			}
			else
				*p++ = ten + 0x9e;
		}

		else if (l == 2)		/* JIS X 0213 plane 1? */
		{
			ku = c1 - 0xa0;
			ten = euc[1] - 0xa0;

			if (ku >= 1 && ku <= 62)
				*p++ = (ku + 0x101) >> 1;
			else if (ku >= 63 && ku <= 94)
				*p++ = (ku + 0x181) >> 1;
			else
			{
				if (noError)
					break;
				report_invalid_encoding(PG_EUC_JIS_2004,
										(const char *) euc, len);
			}

			if (ku % 2)
			{
				if (ten >= 1 && ten <= 63)
					*p++ = ten + 0x3f;
				else if (ten >= 64 && ten <= 94)
					*p++ = ten + 0x40;
				else
				{
					if (noError)
						break;
					report_invalid_encoding(PG_EUC_JIS_2004,
											(const char *) euc, len);
				}
			}
			else
				*p++ = ten + 0x9e;
		}
		else
		{
			if (noError)
				break;
			report_invalid_encoding(PG_EUC_JIS_2004,
									(const char *) euc, len);
		}

		euc += l;
		len -= l;
	}
	*p = '\0';

	return euc - start;
}

/*
 * returns SHIFT_JIS_2004 "ku" code indicated by second byte
 * *ku = 0: "ku" = even
 * *ku = 1: "ku" = odd
 */
static int
get_ten(int b, int *ku)
{
	int			ten;

	if (b >= 0x40 && b <= 0x7e)
	{
		ten = b - 0x3f;
		*ku = 1;
	}
	else if (b >= 0x80 && b <= 0x9e)
	{
		ten = b - 0x40;
		*ku = 1;
	}
	else if (b >= 0x9f && b <= 0xfc)
	{
		ten = b - 0x9e;
		*ku = 0;
	}
	else
	{
		ten = -1;				/* error */
		*ku = 0;				/* keep compiler quiet */
	}
	return ten;
}

/*
 * SHIFT_JIS_2004 ---> EUC_JIS_2004
 */

static int
shift_jis_20042euc_jis_2004(const unsigned char *sjis, unsigned char *p, int len, bool noError)
{
	const unsigned char *start = sjis;
	int			c1;
	int			ku,
				ten,
				kubun;
	int			plane;
	int			l;

	while (len > 0)
	{
		c1 = *sjis;

		if (!IS_HIGHBIT_SET(c1))
		{
			/* ASCII */
			if (c1 == 0)
			{
				if (noError)
					break;
				report_invalid_encoding(PG_SHIFT_JIS_2004,
										(const char *) sjis, len);
			}
			*p++ = c1;
			sjis++;
			len--;
			continue;
		}

		l = pg_encoding_verifymbchar(PG_SHIFT_JIS_2004, (const char *) sjis, len);

		if (l < 0 || l > len)
		{
			if (noError)
				break;
			report_invalid_encoding(PG_SHIFT_JIS_2004,
									(const char *) sjis, len);
		}

		if (c1 >= 0xa1 && c1 <= 0xdf && l == 1)
		{
			/* JIS X0201 (1 byte kana) */
			*p++ = SS2;
			*p++ = c1;
		}
		else if (l == 2)
		{
			int			c2 = sjis[1];

			plane = 1;
			ku = 1;
			ten = 1;

			/*
			 * JIS X 0213
			 */
			if (c1 >= 0x81 && c1 <= 0x9f)	/* plane 1 1ku-62ku */
			{
				ku = (c1 << 1) - 0x100;
				ten = get_ten(c2, &kubun);
				if (ten < 0)
				{
					if (noError)
						break;
					report_invalid_encoding(PG_SHIFT_JIS_2004,
											(const char *) sjis, len);
				}
				ku -= kubun;
			}
			else if (c1 >= 0xe0 && c1 <= 0xef)	/* plane 1 62ku-94ku */
			{
				ku = (c1 << 1) - 0x180;
				ten = get_ten(c2, &kubun);
				if (ten < 0)
				{
					if (noError)
						break;
					report_invalid_encoding(PG_SHIFT_JIS_2004,
											(const char *) sjis, len);
				}
				ku -= kubun;
			}
			else if (c1 >= 0xf0 && c1 <= 0xf3)	/* plane 2
												 * 1,3,4,5,8,12,13,14,15 ku */
			{
				plane = 2;
				ten = get_ten(c2, &kubun);
				if (ten < 0)
				{
					if (noError)
						break;
					report_invalid_encoding(PG_SHIFT_JIS_2004,
											(const char *) sjis, len);
				}
				switch (c1)
				{
					case 0xf0:
						ku = kubun == 0 ? 8 : 1;
						break;
					case 0xf1:
						ku = kubun == 0 ? 4 : 3;
						break;
					case 0xf2:
						ku = kubun == 0 ? 12 : 5;
						break;
					default:
						ku = kubun == 0 ? 14 : 13;
						break;
				}
			}
			else if (c1 >= 0xf4 && c1 <= 0xfc)	/* plane 2 78-94ku */
			{
				plane = 2;
				ten = get_ten(c2, &kubun);
				if (ten < 0)
				{
					if (noError)
						break;
					report_invalid_encoding(PG_SHIFT_JIS_2004,
											(const char *) sjis, len);
				}
				if (c1 == 0xf4 && kubun == 1)
					ku = 15;
				else
					ku = (c1 << 1) - 0x19a - kubun;
			}
			else
			{
				if (noError)
					break;
				report_invalid_encoding(PG_SHIFT_JIS_2004,
										(const char *) sjis, len);
			}

			if (plane == 2)
				*p++ = SS3;

			*p++ = ku + 0xa0;
			*p++ = ten + 0xa0;
		}
		sjis += l;
		len -= l;
	}
	*p = '\0';

	return sjis - start;
}

/*-------------------------------------------------------------------------
 *
 * pg_wchar.h
 *	  multibyte-character support
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/mb/pg_wchar.h
 *
 *	NOTES
 *		This is used both by the backend and by frontends, but should not be
 *		included by libpq client programs.  In particular, a libpq client
 *		should not assume that the encoding IDs used by the version of libpq
 *		it's linked to match up with the IDs declared here.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_WCHAR_H
#define PG_WCHAR_H

/*
 * The pg_wchar type
 */
typedef unsigned int pg_wchar;

/*
 * Maximum byte length of multibyte characters in any backend encoding
 */
#define MAX_MULTIBYTE_CHAR_LEN	4

/*
 * various definitions for EUC
 */
#define SS2 0x8e				/* single shift 2 (JIS0201) */
#define SS3 0x8f				/* single shift 3 (JIS0212) */

/*
 * SJIS validation macros
 */
#define ISSJISHEAD(c) (((c) >= 0x81 && (c) <= 0x9f) || ((c) >= 0xe0 && (c) <= 0xfc))
#define ISSJISTAIL(c) (((c) >= 0x40 && (c) <= 0x7e) || ((c) >= 0x80 && (c) <= 0xfc))

/*----------------------------------------------------
 * MULE Internal Encoding (MIC)
 *
 * This encoding follows the design used within XEmacs; it is meant to
 * subsume many externally-defined character sets.  Each character includes
 * identification of the character set it belongs to, so the encoding is
 * general but somewhat bulky.
 *
 * Currently PostgreSQL supports 5 types of MULE character sets:
 *
 * 1) 1-byte ASCII characters.  Each byte is below 0x80.
 *
 * 2) "Official" single byte charsets such as ISO-8859-1 (Latin1).
 *	  Each MULE character consists of 2 bytes: LC1 + C1, where LC1 is
 *	  an identifier for the charset (in the range 0x81 to 0x8d) and C1
 *	  is the character code (in the range 0xa0 to 0xff).
 *
 * 3) "Private" single byte charsets such as SISHENG.  Each MULE
 *	  character consists of 3 bytes: LCPRV1 + LC12 + C1, where LCPRV1
 *	  is a private-charset flag, LC12 is an identifier for the charset,
 *	  and C1 is the character code (in the range 0xa0 to 0xff).
 *	  LCPRV1 is either 0x9a (if LC12 is in the range 0xa0 to 0xdf)
 *	  or 0x9b (if LC12 is in the range 0xe0 to 0xef).
 *
 * 4) "Official" multibyte charsets such as JIS X0208.  Each MULE
 *	  character consists of 3 bytes: LC2 + C1 + C2, where LC2 is
 *	  an identifier for the charset (in the range 0x90 to 0x99) and C1
 *	  and C2 form the character code (each in the range 0xa0 to 0xff).
 *
 * 5) "Private" multibyte charsets such as CNS 11643-1992 Plane 3.
 *	  Each MULE character consists of 4 bytes: LCPRV2 + LC22 + C1 + C2,
 *	  where LCPRV2 is a private-charset flag, LC22 is an identifier for
 *	  the charset, and C1 and C2 form the character code (each in the range
 *	  0xa0 to 0xff).  LCPRV2 is either 0x9c (if LC22 is in the range 0xf0
 *	  to 0xf4) or 0x9d (if LC22 is in the range 0xf5 to 0xfe).
 *
 * "Official" encodings are those that have been assigned code numbers by
 * the XEmacs project; "private" encodings have Postgres-specific charset
 * identifiers.
 *
 * See the "XEmacs Internals Manual", available at http://www.xemacs.org,
 * for more details.  Note that for historical reasons, Postgres'
 * private-charset flag values do not match what XEmacs says they should be,
 * so this isn't really exactly MULE (not that private charsets would be
 * interoperable anyway).
 *
 * Note that XEmacs's implementation is different from what emacs does.
 * We follow emacs's implementation, rather than XEmacs's.
 *----------------------------------------------------
 */

/*
 * Charset identifiers (also called "leading bytes" in the MULE documentation)
 */

/*
 * Charset IDs for official single byte encodings (0x81-0x8e)
 */
#define LC_ISO8859_1		0x81	/* ISO8859 Latin 1 */
#define LC_ISO8859_2		0x82	/* ISO8859 Latin 2 */
#define LC_ISO8859_3		0x83	/* ISO8859 Latin 3 */
#define LC_ISO8859_4		0x84	/* ISO8859 Latin 4 */
#define LC_TIS620			0x85	/* Thai (not supported yet) */
#define LC_ISO8859_7		0x86	/* Greek (not supported yet) */
#define LC_ISO8859_6		0x87	/* Arabic (not supported yet) */
#define LC_ISO8859_8		0x88	/* Hebrew (not supported yet) */
#define LC_JISX0201K		0x89	/* Japanese 1 byte kana */
#define LC_JISX0201R		0x8a	/* Japanese 1 byte Roman */
/* Note that 0x8b seems to be unused as of Emacs 20.7.
 * However, there might be a chance that 0x8b could be used
 * in later versions of Emacs.
 */
#define LC_KOI8_R			0x8b	/* Cyrillic KOI8-R */
#define LC_ISO8859_5		0x8c	/* ISO8859 Cyrillic */
#define LC_ISO8859_9		0x8d	/* ISO8859 Latin 5 (not supported yet) */
#define LC_ISO8859_15		0x8e	/* ISO8859 Latin 15 (not supported yet) */
/* #define CONTROL_1		0x8f	control characters (unused) */

/* Is a leading byte for "official" single byte encodings? */
#define IS_LC1(c)	((unsigned char)(c) >= 0x81 && (unsigned char)(c) <= 0x8d)

/*
 * Charset IDs for official multibyte encodings (0x90-0x99)
 * 0x9a-0x9d are free. 0x9e and 0x9f are reserved.
 */
#define LC_JISX0208_1978	0x90	/* Japanese Kanji, old JIS (not supported) */
#define LC_GB2312_80		0x91	/* Chinese */
#define LC_JISX0208			0x92	/* Japanese Kanji (JIS X 0208) */
#define LC_KS5601			0x93	/* Korean */
#define LC_JISX0212			0x94	/* Japanese Kanji (JIS X 0212) */
#define LC_CNS11643_1		0x95	/* CNS 11643-1992 Plane 1 */
#define LC_CNS11643_2		0x96	/* CNS 11643-1992 Plane 2 */
#define LC_JISX0213_1		0x97	/* Japanese Kanji (JIS X 0213 Plane 1)
									 * (not supported) */
#define LC_BIG5_1			0x98	/* Plane 1 Chinese traditional (not
									 * supported) */
#define LC_BIG5_2			0x99	/* Plane 1 Chinese traditional (not
									 * supported) */

/* Is a leading byte for "official" multibyte encodings? */
#define IS_LC2(c)	((unsigned char)(c) >= 0x90 && (unsigned char)(c) <= 0x99)

/*
 * Postgres-specific prefix bytes for "private" single byte encodings
 * (According to the MULE docs, we should be using 0x9e for this)
 */
#define LCPRV1_A		0x9a
#define LCPRV1_B		0x9b
#define IS_LCPRV1(c)	((unsigned char)(c) == LCPRV1_A || (unsigned char)(c) == LCPRV1_B)
#define IS_LCPRV1_A_RANGE(c)	\
	((unsigned char)(c) >= 0xa0 && (unsigned char)(c) <= 0xdf)
#define IS_LCPRV1_B_RANGE(c)	\
	((unsigned char)(c) >= 0xe0 && (unsigned char)(c) <= 0xef)

/*
 * Postgres-specific prefix bytes for "private" multibyte encodings
 * (According to the MULE docs, we should be using 0x9f for this)
 */
#define LCPRV2_A		0x9c
#define LCPRV2_B		0x9d
#define IS_LCPRV2(c)	((unsigned char)(c) == LCPRV2_A || (unsigned char)(c) == LCPRV2_B)
#define IS_LCPRV2_A_RANGE(c)	\
	((unsigned char)(c) >= 0xf0 && (unsigned char)(c) <= 0xf4)
#define IS_LCPRV2_B_RANGE(c)	\
	((unsigned char)(c) >= 0xf5 && (unsigned char)(c) <= 0xfe)

/*
 * Charset IDs for private single byte encodings (0xa0-0xef)
 */
#define LC_SISHENG			0xa0	/* Chinese SiSheng characters for
									 * PinYin/ZhuYin (not supported) */
#define LC_IPA				0xa1	/* IPA (International Phonetic
									 * Association) (not supported) */
#define LC_VISCII_LOWER		0xa2	/* Vietnamese VISCII1.1 lower-case (not
									 * supported) */
#define LC_VISCII_UPPER		0xa3	/* Vietnamese VISCII1.1 upper-case (not
									 * supported) */
#define LC_ARABIC_DIGIT		0xa4	/* Arabic digit (not supported) */
#define LC_ARABIC_1_COLUMN	0xa5	/* Arabic 1-column (not supported) */
#define LC_ASCII_RIGHT_TO_LEFT	0xa6	/* ASCII (left half of ISO8859-1) with
										 * right-to-left direction (not
										 * supported) */
#define LC_LAO				0xa7	/* Lao characters (ISO10646 0E80..0EDF)
									 * (not supported) */
#define LC_ARABIC_2_COLUMN	0xa8	/* Arabic 1-column (not supported) */

/*
 * Charset IDs for private multibyte encodings (0xf0-0xff)
 */
#define LC_INDIAN_1_COLUMN	0xf0	/* Indian charset for 1-column width
									 * glyphs (not supported) */
#define LC_TIBETAN_1_COLUMN 0xf1	/* Tibetan 1-column width glyphs (not
									 * supported) */
#define LC_UNICODE_SUBSET_2 0xf2	/* Unicode characters of the range
									 * U+2500..U+33FF. (not supported) */
#define LC_UNICODE_SUBSET_3 0xf3	/* Unicode characters of the range
									 * U+E000..U+FFFF. (not supported) */
#define LC_UNICODE_SUBSET	0xf4	/* Unicode characters of the range
									 * U+0100..U+24FF. (not supported) */
#define LC_ETHIOPIC			0xf5	/* Ethiopic characters (not supported) */
#define LC_CNS11643_3		0xf6	/* CNS 11643-1992 Plane 3 */
#define LC_CNS11643_4		0xf7	/* CNS 11643-1992 Plane 4 */
#define LC_CNS11643_5		0xf8	/* CNS 11643-1992 Plane 5 */
#define LC_CNS11643_6		0xf9	/* CNS 11643-1992 Plane 6 */
#define LC_CNS11643_7		0xfa	/* CNS 11643-1992 Plane 7 */
#define LC_INDIAN_2_COLUMN	0xfb	/* Indian charset for 2-column width
									 * glyphs (not supported) */
#define LC_TIBETAN			0xfc	/* Tibetan (not supported) */
/* #define FREE				0xfd	free (unused) */
/* #define FREE				0xfe	free (unused) */
/* #define FREE				0xff	free (unused) */

/*----------------------------------------------------
 * end of MULE stuff
 *----------------------------------------------------
 */

/*
 * PostgreSQL encoding identifiers
 *
 * WARNING: the order of this enum must be same as order of entries
 *			in the pg_enc2name_tbl[] array (in src/common/encnames.c), and
 *			in the pg_wchar_table[] array (in src/common/wchar.c)!
 *
 *			If you add some encoding don't forget to check
 *			PG_ENCODING_BE_LAST macro.
 *
 * PG_SQL_ASCII is default encoding and must be = 0.
 *
 * XXX	We must avoid renumbering any backend encoding until libpq's major
 * version number is increased beyond 5; it turns out that the backend
 * encoding IDs are effectively part of libpq's ABI as far as 8.2 initdb and
 * psql are concerned.
 */
typedef enum pg_enc
{
	PG_SQL_ASCII = 0,			/* SQL/ASCII */
	PG_EUC_JP,					/* EUC for Japanese */
	PG_EUC_CN,					/* EUC for Chinese */
	PG_EUC_KR,					/* EUC for Korean */
	PG_EUC_TW,					/* EUC for Taiwan */
	PG_EUC_JIS_2004,			/* EUC-JIS-2004 */
	PG_UTF8,					/* Unicode UTF8 */
	PG_MULE_INTERNAL,			/* Mule internal code */
	PG_LATIN1,					/* ISO-8859-1 Latin 1 */
	PG_LATIN2,					/* ISO-8859-2 Latin 2 */
	PG_LATIN3,					/* ISO-8859-3 Latin 3 */
	PG_LATIN4,					/* ISO-8859-4 Latin 4 */
	PG_LATIN5,					/* ISO-8859-9 Latin 5 */
	PG_LATIN6,					/* ISO-8859-10 Latin6 */
	PG_LATIN7,					/* ISO-8859-13 Latin7 */
	PG_LATIN8,					/* ISO-8859-14 Latin8 */
	PG_LATIN9,					/* ISO-8859-15 Latin9 */
	PG_LATIN10,					/* ISO-8859-16 Latin10 */
	PG_WIN1256,					/* windows-1256 */
	PG_WIN1258,					/* Windows-1258 */
	PG_WIN866,					/* (MS-DOS CP866) */
	PG_WIN874,					/* windows-874 */
	PG_KOI8R,					/* KOI8-R */
	PG_WIN1251,					/* windows-1251 */
	PG_WIN1252,					/* windows-1252 */
	PG_ISO_8859_5,				/* ISO-8859-5 */
	PG_ISO_8859_6,				/* ISO-8859-6 */
	PG_ISO_8859_7,				/* ISO-8859-7 */
	PG_ISO_8859_8,				/* ISO-8859-8 */
	PG_WIN1250,					/* windows-1250 */
	PG_WIN1253,					/* windows-1253 */
	PG_WIN1254,					/* windows-1254 */
	PG_WIN1255,					/* windows-1255 */
	PG_WIN1257,					/* windows-1257 */
	PG_KOI8U,					/* KOI8-U */
	/* PG_ENCODING_BE_LAST points to the above entry */

	/* followings are for client encoding only */
	PG_SJIS,					/* Shift JIS (Windows-932) */
	PG_BIG5,					/* Big5 (Windows-950) */
	PG_GBK,						/* GBK (Windows-936) */
	PG_UHC,						/* UHC (Windows-949) */
	PG_GB18030,					/* GB18030 */
	PG_JOHAB,					/* EUC for Korean JOHAB */
	PG_SHIFT_JIS_2004,			/* Shift-JIS-2004 */
	_PG_LAST_ENCODING_			/* mark only */

} pg_enc;

#define PG_ENCODING_BE_LAST PG_KOI8U

/*
 * Please use these tests before access to pg_enc2name_tbl[]
 * or to other places...
 */
#define PG_VALID_BE_ENCODING(_enc) \
		((_enc) >= 0 && (_enc) <= PG_ENCODING_BE_LAST)

#define PG_ENCODING_IS_CLIENT_ONLY(_enc) \
		((_enc) > PG_ENCODING_BE_LAST && (_enc) < _PG_LAST_ENCODING_)

#define PG_VALID_ENCODING(_enc) \
		((_enc) >= 0 && (_enc) < _PG_LAST_ENCODING_)

/* On FE are possible all encodings */
#define PG_VALID_FE_ENCODING(_enc)	PG_VALID_ENCODING(_enc)

/*
 * When converting strings between different encodings, we assume that space
 * for converted result is 4-to-1 growth in the worst case.  The rate for
 * currently supported encoding pairs are within 3 (SJIS JIS X0201 half width
 * kana -> UTF8 is the worst case).  So "4" should be enough for the moment.
 *
 * Note that this is not the same as the maximum character width in any
 * particular encoding.
 */
#define MAX_CONVERSION_GROWTH  4

/*
 * Maximum byte length of a string that's required in any encoding to convert
 * at least one character to any other encoding.  In other words, if you feed
 * MAX_CONVERSION_INPUT_LENGTH bytes to any encoding conversion function, it
 * is guaranteed to be able to convert something without needing more input
 * (assuming the input is valid).
 *
 * Currently, the maximum case is the conversion UTF8 -> SJIS JIS X0201 half
 * width kana, where a pair of UTF-8 characters is converted into a single
 * SHIFT_JIS_2004 character (the reverse of the worst case for
 * MAX_CONVERSION_GROWTH).  It needs 6 bytes of input.  In theory, a
 * user-defined conversion function might have more complicated cases, although
 * for the reverse mapping you would probably also need to bump up
 * MAX_CONVERSION_GROWTH.  But there is no need to be stingy here, so make it
 * generous.
 */
#define MAX_CONVERSION_INPUT_LENGTH	16

/*
 * Maximum byte length of the string equivalent to any one Unicode code point,
 * in any backend encoding.  The current value assumes that a 4-byte UTF-8
 * character might expand by MAX_CONVERSION_GROWTH, which is a huge
 * overestimate.  But in current usage we don't allocate large multiples of
 * this, so there's little point in being stingy.
 */
#define MAX_UNICODE_EQUIVALENT_STRING	16

/*
 * Table for mapping an encoding number to official encoding name and
 * possibly other subsidiary data.  Be careful to check encoding number
 * before accessing a table entry!
 *
 * if (PG_VALID_ENCODING(encoding))
 *		pg_enc2name_tbl[ encoding ];
 */
typedef struct pg_enc2name
{
	const char *name;
	pg_enc		encoding;
#ifdef WIN32
	unsigned	codepage;		/* codepage for WIN32 */
#endif
} pg_enc2name;

extern PGDLLIMPORT const pg_enc2name pg_enc2name_tbl[];

/*
 * Encoding names for gettext
 */
typedef struct pg_enc2gettext
{
	pg_enc		encoding;
	const char *name;
} pg_enc2gettext;

extern PGDLLIMPORT const pg_enc2gettext pg_enc2gettext_tbl[];

/*
 * pg_wchar stuff
 */
typedef int (*mb2wchar_with_len_converter) (const unsigned char *from,
											pg_wchar *to,
											int len);

typedef int (*wchar2mb_with_len_converter) (const pg_wchar *from,
											unsigned char *to,
											int len);

typedef int (*mblen_converter) (const unsigned char *mbstr);

typedef int (*mbdisplaylen_converter) (const unsigned char *mbstr);

typedef bool (*mbcharacter_incrementer) (unsigned char *mbstr, int len);

typedef int (*mbchar_verifier) (const unsigned char *mbstr, int len);

typedef int (*mbstr_verifier) (const unsigned char *mbstr, int len);

typedef struct
{
	mb2wchar_with_len_converter mb2wchar_with_len;	/* convert a multibyte
													 * string to a wchar */
	wchar2mb_with_len_converter wchar2mb_with_len;	/* convert a wchar string
													 * to a multibyte */
	mblen_converter mblen;		/* get byte length of a char */
	mbdisplaylen_converter dsplen;	/* get display width of a char */
	mbchar_verifier mbverifychar;	/* verify multibyte character */
	mbstr_verifier mbverifystr; /* verify multibyte string */
	int			maxmblen;		/* max bytes for a char in this encoding */
} pg_wchar_tbl;

extern PGDLLIMPORT const pg_wchar_tbl pg_wchar_table[];

/*
 * Data structures for conversions between UTF-8 and other encodings
 * (UtfToLocal() and LocalToUtf()).  In these data structures, characters of
 * either encoding are represented by uint32 words; hence we can only support
 * characters up to 4 bytes long.  For example, the byte sequence 0xC2 0x89
 * would be represented by 0x0000C289, and 0xE8 0xA2 0xB4 by 0x00E8A2B4.
 *
 * There are three possible ways a character can be mapped:
 *
 * 1. Using a radix tree, from source to destination code.
 * 2. Using a sorted array of source -> destination code pairs. This
 *	  method is used for "combining" characters. There are so few of
 *	  them that building a radix tree would be wasteful.
 * 3. Using a conversion function.
 */

/*
 * Radix tree for character conversion.
 *
 * Logically, this is actually four different radix trees, for 1-byte,
 * 2-byte, 3-byte and 4-byte inputs. The 1-byte tree is a simple lookup
 * table from source to target code. The 2-byte tree consists of two levels:
 * one lookup table for the first byte, where the value in the lookup table
 * points to a lookup table for the second byte. And so on.
 *
 * Physically, all the trees are stored in one big array, in 'chars16' or
 * 'chars32', depending on the maximum value that needs to be represented. For
 * each level in each tree, we also store lower and upper bound of allowed
 * values - values outside those bounds are considered invalid, and are left
 * out of the tables.
 *
 * In the intermediate levels of the trees, the values stored are offsets
 * into the chars[16|32] array.
 *
 * In the beginning of the chars[16|32] array, there is always a number of
 * zeros, so that you safely follow an index from an intermediate table
 * without explicitly checking for a zero. Following a zero any number of
 * times will always bring you to the dummy, all-zeros table in the
 * beginning. This helps to shave some cycles when looking up values.
 */
typedef struct
{
	/*
	 * Array containing all the values. Only one of chars16 or chars32 is
	 * used, depending on how wide the values we need to represent are.
	 */
	const uint16 *chars16;
	const uint32 *chars32;

	/* Radix tree for 1-byte inputs */
	uint32		b1root;			/* offset of table in the chars[16|32] array */
	uint8		b1_lower;		/* min allowed value for a single byte input */
	uint8		b1_upper;		/* max allowed value for a single byte input */

	/* Radix tree for 2-byte inputs */
	uint32		b2root;			/* offset of 1st byte's table */
	uint8		b2_1_lower;		/* min/max allowed value for 1st input byte */
	uint8		b2_1_upper;
	uint8		b2_2_lower;		/* min/max allowed value for 2nd input byte */
	uint8		b2_2_upper;

	/* Radix tree for 3-byte inputs */
	uint32		b3root;			/* offset of 1st byte's table */
	uint8		b3_1_lower;		/* min/max allowed value for 1st input byte */
	uint8		b3_1_upper;
	uint8		b3_2_lower;		/* min/max allowed value for 2nd input byte */
	uint8		b3_2_upper;
	uint8		b3_3_lower;		/* min/max allowed value for 3rd input byte */
	uint8		b3_3_upper;

	/* Radix tree for 4-byte inputs */
	uint32		b4root;			/* offset of 1st byte's table */
	uint8		b4_1_lower;		/* min/max allowed value for 1st input byte */
	uint8		b4_1_upper;
	uint8		b4_2_lower;		/* min/max allowed value for 2nd input byte */
	uint8		b4_2_upper;
	uint8		b4_3_lower;		/* min/max allowed value for 3rd input byte */
	uint8		b4_3_upper;
	uint8		b4_4_lower;		/* min/max allowed value for 4th input byte */
	uint8		b4_4_upper;

} pg_mb_radix_tree;

/*
 * UTF-8 to local code conversion map (for combined characters)
 */
typedef struct
{
	uint32		utf1;			/* UTF-8 code 1 */
	uint32		utf2;			/* UTF-8 code 2 */
	uint32		code;			/* local code */
} pg_utf_to_local_combined;

/*
 * local code to UTF-8 conversion map (for combined characters)
 */
typedef struct
{
	uint32		code;			/* local code */
	uint32		utf1;			/* UTF-8 code 1 */
	uint32		utf2;			/* UTF-8 code 2 */
} pg_local_to_utf_combined;

/*
 * callback function for algorithmic encoding conversions (in either direction)
 *
 * if function returns zero, it does not know how to convert the code
 */
typedef uint32 (*utf_local_conversion_func) (uint32 code);

/*
 * Support macro for encoding conversion functions to validate their
 * arguments.  (This could be made more compact if we included fmgr.h
 * here, but we don't want to do that because this header file is also
 * used by frontends.)
 */
#define CHECK_ENCODING_CONVERSION_ARGS(srcencoding,destencoding) \
	check_encoding_conversion_args(PG_GETARG_INT32(0), \
								   PG_GETARG_INT32(1), \
								   PG_GETARG_INT32(4), \
								   (srcencoding), \
								   (destencoding))


/*
 * Some handy functions for Unicode-specific tests.
 */
static inline bool
is_valid_unicode_codepoint(pg_wchar c)
{
	return (c > 0 && c <= 0x10FFFF);
}

static inline bool
is_utf16_surrogate_first(pg_wchar c)
{
	return (c >= 0xD800 && c <= 0xDBFF);
}

static inline bool
is_utf16_surrogate_second(pg_wchar c)
{
	return (c >= 0xDC00 && c <= 0xDFFF);
}

static inline pg_wchar
surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second)
{
	return ((first & 0x3FF) << 10) + 0x10000 + (second & 0x3FF);
}


/*
 * These functions are considered part of libpq's exported API and
 * are also declared in libpq-fe.h.
 */
extern int	pg_char_to_encoding(const char *name);
extern const char *pg_encoding_to_char(int encoding);
extern int	pg_valid_server_encoding_id(int encoding);

/*
 * These functions are available to frontend code that links with libpgcommon
 * (in addition to the ones just above).  The constant tables declared
 * earlier in this file are also available from libpgcommon.
 */
extern int	pg_encoding_mblen(int encoding, const char *mbstr);
extern int	pg_encoding_mblen_bounded(int encoding, const char *mbstr);
extern int	pg_encoding_dsplen(int encoding, const char *mbstr);
extern int	pg_encoding_verifymbchar(int encoding, const char *mbstr, int len);
extern int	pg_encoding_verifymbstr(int encoding, const char *mbstr, int len);
extern int	pg_encoding_max_length(int encoding);
extern int	pg_valid_client_encoding(const char *name);
extern int	pg_valid_server_encoding(const char *name);
extern bool is_encoding_supported_by_icu(int encoding);
extern const char *get_encoding_name_for_icu(int encoding);

extern unsigned char *unicode_to_utf8(pg_wchar c, unsigned char *utf8string);
extern pg_wchar utf8_to_unicode(const unsigned char *c);
extern bool pg_utf8_islegal(const unsigned char *source, int length);
extern int	pg_utf_mblen(const unsigned char *s);
extern int	pg_mule_mblen(const unsigned char *s);

/*
 * The remaining functions are backend-only.
 */
extern int	pg_mb2wchar(const char *from, pg_wchar *to);
extern int	pg_mb2wchar_with_len(const char *from, pg_wchar *to, int len);
extern int	pg_encoding_mb2wchar_with_len(int encoding,
										  const char *from, pg_wchar *to, int len);
extern int	pg_wchar2mb(const pg_wchar *from, char *to);
extern int	pg_wchar2mb_with_len(const pg_wchar *from, char *to, int len);
extern int	pg_encoding_wchar2mb_with_len(int encoding,
										  const pg_wchar *from, char *to, int len);
extern int	pg_char_and_wchar_strcmp(const char *s1, const pg_wchar *s2);
extern int	pg_wchar_strncmp(const pg_wchar *s1, const pg_wchar *s2, size_t n);
extern int	pg_char_and_wchar_strncmp(const char *s1, const pg_wchar *s2, size_t n);
extern size_t pg_wchar_strlen(const pg_wchar *wstr);
extern int	pg_mblen(const char *mbstr);
extern int	pg_dsplen(const char *mbstr);
extern int	pg_mbstrlen(const char *mbstr);
extern int	pg_mbstrlen_with_len(const char *mbstr, int len);
extern int	pg_mbcliplen(const char *mbstr, int len, int limit);
extern int	pg_encoding_mbcliplen(int encoding, const char *mbstr,
								  int len, int limit);
extern int	pg_mbcharcliplen(const char *mbstr, int len, int limit);
extern int	pg_database_encoding_max_length(void);
extern mbcharacter_incrementer pg_database_encoding_character_incrementer(void);

extern int	PrepareClientEncoding(int encoding);
extern int	SetClientEncoding(int encoding);
extern void InitializeClientEncoding(void);
extern int	pg_get_client_encoding(void);
extern const char *pg_get_client_encoding_name(void);

extern void SetDatabaseEncoding(int encoding);
extern int	GetDatabaseEncoding(void);
extern const char *GetDatabaseEncodingName(void);
extern void SetMessageEncoding(int encoding);
extern int	GetMessageEncoding(void);

#ifdef ENABLE_NLS
extern int	pg_bind_textdomain_codeset(const char *domainname);
#endif

extern unsigned char *pg_do_encoding_conversion(unsigned char *src, int len,
												int src_encoding,
												int dest_encoding);
extern int	pg_do_encoding_conversion_buf(Oid proc,
										  int src_encoding,
										  int dest_encoding,
										  unsigned char *src, int srclen,
										  unsigned char *dst, int dstlen,
										  bool noError);

extern char *pg_client_to_server(const char *s, int len);
extern char *pg_server_to_client(const char *s, int len);
extern char *pg_any_to_server(const char *s, int len, int encoding);
extern char *pg_server_to_any(const char *s, int len, int encoding);

extern void pg_unicode_to_server(pg_wchar c, unsigned char *s);

extern unsigned short BIG5toCNS(unsigned short big5, unsigned char *lc);
extern unsigned short CNStoBIG5(unsigned short cns, unsigned char lc);

extern int	UtfToLocal(const unsigned char *utf, int len,
					   unsigned char *iso,
					   const pg_mb_radix_tree *map,
					   const pg_utf_to_local_combined *cmap, int cmapsize,
					   utf_local_conversion_func conv_func,
					   int encoding, bool noError);
extern int	LocalToUtf(const unsigned char *iso, int len,
					   unsigned char *utf,
					   const pg_mb_radix_tree *map,
					   const pg_local_to_utf_combined *cmap, int cmapsize,
					   utf_local_conversion_func conv_func,
					   int encoding, bool noError);

extern bool pg_verifymbstr(const char *mbstr, int len, bool noError);
extern bool pg_verify_mbstr(int encoding, const char *mbstr, int len,
							bool noError);
extern int	pg_verify_mbstr_len(int encoding, const char *mbstr, int len,
								bool noError);

extern void check_encoding_conversion_args(int src_encoding,
										   int dest_encoding,
										   int len,
										   int expected_src_encoding,
										   int expected_dest_encoding);

extern void report_invalid_encoding(int encoding, const char *mbstr, int len) pg_attribute_noreturn();
extern void report_untranslatable_char(int src_encoding, int dest_encoding,
									   const char *mbstr, int len) pg_attribute_noreturn();

extern int	local2local(const unsigned char *l, unsigned char *p, int len,
						int src_encoding, int dest_encoding,
						const unsigned char *tab, bool noError);
extern int	latin2mic(const unsigned char *l, unsigned char *p, int len,
					  int lc, int encoding, bool noError);
extern int	mic2latin(const unsigned char *mic, unsigned char *p, int len,
					  int lc, int encoding, bool noError);
extern int	latin2mic_with_table(const unsigned char *l, unsigned char *p,
								 int len, int lc, int encoding,
								 const unsigned char *tab, bool noError);
extern int	mic2latin_with_table(const unsigned char *mic, unsigned char *p,
								 int len, int lc, int encoding,
								 const unsigned char *tab, bool noError);

#ifdef WIN32
extern WCHAR *pgwin32_message_to_UTF16(const char *str, int len, int *utf16len);
#endif


/*
 * Verify a chunk of bytes for valid ASCII.
 *
 * Returns false if the input contains any zero bytes or bytes with the
 * high-bit set. Input len must be a multiple of 8.
 */
static inline bool
is_valid_ascii(const unsigned char *s, int len)
{
	uint64		chunk,
				highbit_cum = UINT64CONST(0),
				zero_cum = UINT64CONST(0x8080808080808080);

	Assert(len % sizeof(chunk) == 0);

	while (len > 0)
	{
		memcpy(&chunk, s, sizeof(chunk));

		/*
		 * Capture any zero bytes in this chunk.
		 *
		 * First, add 0x7f to each byte. This sets the high bit in each byte,
		 * unless it was a zero. If any resulting high bits are zero, the
		 * corresponding high bits in the zero accumulator will be cleared.
		 *
		 * If none of the bytes in the chunk had the high bit set, the max
		 * value each byte can have after the addition is 0x7f + 0x7f = 0xfe,
		 * and we don't need to worry about carrying over to the next byte. If
		 * any input bytes did have the high bit set, it doesn't matter
		 * because we check for those separately.
		 */
		zero_cum &= (chunk + UINT64CONST(0x7f7f7f7f7f7f7f7f));

		/* Capture any set bits in this chunk. */
		highbit_cum |= chunk;

		s += sizeof(chunk);
		len -= sizeof(chunk);
	}

	/* Check if any high bits in the high bit accumulator got set. */
	if (highbit_cum & UINT64CONST(0x8080808080808080))
		return false;

	/* Check if any high bits in the zero accumulator got cleared. */
	if (zero_cum != UINT64CONST(0x8080808080808080))
		return false;

	return true;
}

#endif							/* PG_WCHAR_H */

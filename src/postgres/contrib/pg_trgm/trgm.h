/*
 * contrib/pg_trgm/trgm.h
 */
#ifndef __TRGM_H__
#define __TRGM_H__

#include "access/gist.h"
#include "access/itup.h"
#include "access/stratnum.h"
#include "storage/bufpage.h"

/*
 * Options ... but note that trgm_regexp.c effectively assumes these values
 * of LPADDING and RPADDING.
 */
#define LPADDING		2
#define RPADDING		1
#define KEEPONLYALNUM
/*
 * Caution: IGNORECASE macro means that trigrams are case-insensitive.
 * If this macro is disabled, the ~* and ~~* operators must be removed from
 * the operator classes, because we can't handle case-insensitive wildcard
 * search with case-sensitive trigrams.  Failure to do this will result in
 * "cannot handle ~*(~~*) with case-sensitive trigrams" errors.
 */
#define IGNORECASE
#define DIVUNION

/* operator strategy numbers */
#define SimilarityStrategyNumber			1
#define DistanceStrategyNumber				2
#define LikeStrategyNumber					3
#define ILikeStrategyNumber					4
#define RegExpStrategyNumber				5
#define RegExpICaseStrategyNumber			6
#define WordSimilarityStrategyNumber		7
#define WordDistanceStrategyNumber			8
#define StrictWordSimilarityStrategyNumber	9
#define StrictWordDistanceStrategyNumber	10
#define EqualStrategyNumber					11

typedef char trgm[3];

#define CMPCHAR(a,b) ( ((a)==(b)) ? 0 : ( ((a)<(b)) ? -1 : 1 ) )
#define CMPPCHAR(a,b,i)  CMPCHAR( *(((const char*)(a))+i), *(((const char*)(b))+i) )
#define CMPTRGM(a,b) ( CMPPCHAR(a,b,0) ? CMPPCHAR(a,b,0) : ( CMPPCHAR(a,b,1) ? CMPPCHAR(a,b,1) : CMPPCHAR(a,b,2) ) )

#define CPTRGM(a,b) do {				\
	*(((char*)(a))+0) = *(((char*)(b))+0);	\
	*(((char*)(a))+1) = *(((char*)(b))+1);	\
	*(((char*)(a))+2) = *(((char*)(b))+2);	\
} while(0)

#ifdef KEEPONLYALNUM
#define ISWORDCHR(c)	(t_isalpha(c) || t_isdigit(c))
#define ISPRINTABLECHAR(a)	( isascii( *(unsigned char*)(a) ) && (isalnum( *(unsigned char*)(a) ) || *(unsigned char*)(a)==' ') )
#else
#define ISWORDCHR(c)	(!t_isspace(c))
#define ISPRINTABLECHAR(a)	( isascii( *(unsigned char*)(a) ) && isprint( *(unsigned char*)(a) ) )
#endif
#define ISPRINTABLETRGM(t)	( ISPRINTABLECHAR( ((char*)(t)) ) && ISPRINTABLECHAR( ((char*)(t))+1 ) && ISPRINTABLECHAR( ((char*)(t))+2 ) )

#define ISESCAPECHAR(x) (*(x) == '\\')	/* Wildcard escape character */
#define ISWILDCARDCHAR(x) (*(x) == '_' || *(x) == '%')	/* Wildcard
														 * meta-character */

typedef struct
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	uint8		flag;
	char		data[FLEXIBLE_ARRAY_MEMBER];
} TRGM;

#define TRGMHDRSIZE		  (VARHDRSZ + sizeof(uint8))

/* gist */
#define SIGLEN_DEFAULT	(sizeof(int) * 3)
#define SIGLEN_MAX		GISTMaxIndexKeySize
#define BITBYTE 8

#define SIGLENBIT(siglen) ((siglen) * BITBYTE - 1)	/* see makesign */

typedef char *BITVECP;

#define LOOPBYTE(siglen) \
			for (i = 0; i < (siglen); i++)

#define GETBYTE(x,i) ( *( (BITVECP)(x) + (int)( (i) / BITBYTE ) ) )
#define GETBITBYTE(x,i) ( (((char)(x)) >> (i)) & 0x01 )
#define CLRBIT(x,i)   GETBYTE(x,i) &= ~( 0x01 << ( (i) % BITBYTE ) )
#define SETBIT(x,i)   GETBYTE(x,i) |=  ( 0x01 << ( (i) % BITBYTE ) )
#define GETBIT(x,i) ( (GETBYTE(x,i) >> ( (i) % BITBYTE )) & 0x01 )

#define HASHVAL(val, siglen) (((unsigned int)(val)) % SIGLENBIT(siglen))
#define HASH(sign, val, siglen) SETBIT((sign), HASHVAL(val, siglen))

#define ARRKEY			0x01
#define SIGNKEY			0x02
#define ALLISTRUE		0x04

#define ISARRKEY(x) ( ((TRGM*)x)->flag & ARRKEY )
#define ISSIGNKEY(x)	( ((TRGM*)x)->flag & SIGNKEY )
#define ISALLTRUE(x)	( ((TRGM*)x)->flag & ALLISTRUE )

#define CALCGTSIZE(flag, len) ( TRGMHDRSIZE + ( ( (flag) & ARRKEY ) ? ((len)*sizeof(trgm)) : (((flag) & ALLISTRUE) ? 0 : (len)) ) )
#define GETSIGN(x)		( (BITVECP)( (char*)x+TRGMHDRSIZE ) )
#define GETARR(x)		( (trgm*)( (char*)x+TRGMHDRSIZE ) )
#define ARRNELEM(x) ( ( VARSIZE(x) - TRGMHDRSIZE )/sizeof(trgm) )

/*
 * If DIVUNION is defined then similarity formula is:
 * count / (len1 + len2 - count)
 * else if DIVUNION is not defined then similarity formula is:
 * count / max(len1, len2)
 */
#ifdef DIVUNION
#define CALCSML(count, len1, len2) ((float4) (count)) / ((float4) ((len1) + (len2) - (count)))
#else
#define CALCSML(count, len1, len2) ((float4) (count)) / ((float4) (((len1) > (len2)) ? (len1) : (len2)))
#endif

typedef struct TrgmPackedGraph TrgmPackedGraph;

extern double similarity_threshold;
extern double word_similarity_threshold;
extern double strict_word_similarity_threshold;

extern double index_strategy_get_limit(StrategyNumber strategy);
extern uint32 trgm2int(trgm *ptr);
extern void compact_trigram(trgm *tptr, char *str, int bytelen);
extern TRGM *generate_trgm(char *str, int slen);
extern TRGM *generate_wildcard_trgm(const char *str, int slen);
extern float4 cnt_sml(TRGM *trg1, TRGM *trg2, bool inexact);
extern bool trgm_contained_by(TRGM *trg1, TRGM *trg2);
extern bool *trgm_presence_map(TRGM *query, TRGM *key);
extern TRGM *createTrgmNFA(text *text_re, Oid collation,
						   TrgmPackedGraph **graph, MemoryContext rcontext);
extern bool trigramsMatchGraph(TrgmPackedGraph *graph, bool *check);

#endif							/* __TRGM_H__ */

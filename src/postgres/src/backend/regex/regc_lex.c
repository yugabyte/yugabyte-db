/*
 * lexical analyzer
 * This file is #included by regcomp.c.
 *
 * Copyright (c) 1998, 1999 Henry Spencer.  All rights reserved.
 *
 * Development of this software was funded, in part, by Cray Research Inc.,
 * UUNET Communications Services Inc., Sun Microsystems Inc., and Scriptics
 * Corporation, none of whom are responsible for the results.  The author
 * thanks all of them.
 *
 * Redistribution and use in source and binary forms -- with or without
 * modification -- are permitted for any purpose, provided that
 * redistributions in source form retain this entire copyright notice and
 * indicate the origin and nature of any modifications.
 *
 * I'd appreciate being given credit for this package in the documentation
 * of software which uses it, but that is not a requirement.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * HENRY SPENCER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * src/backend/regex/regc_lex.c
 *
 */

/* scanning macros (know about v) */
#define ATEOS()		(v->now >= v->stop)
#define HAVE(n)		(v->stop - v->now >= (n))
#define NEXT1(c)	(!ATEOS() && *v->now == CHR(c))
#define NEXT2(a,b)	(HAVE(2) && *v->now == CHR(a) && *(v->now+1) == CHR(b))
#define NEXT3(a,b,c)	(HAVE(3) && *v->now == CHR(a) && \
						*(v->now+1) == CHR(b) && \
						*(v->now+2) == CHR(c))
#define SET(c)		(v->nexttype = (c))
#define SETV(c, n)	(v->nexttype = (c), v->nextvalue = (n))
#define RET(c)		return (SET(c), 1)
#define RETV(c, n)	return (SETV(c, n), 1)
#define FAILW(e)	return (ERR(e), 0)	/* ERR does SET(EOS) */
#define LASTTYPE(t) (v->lasttype == (t))

/* lexical contexts */
#define L_ERE	1				/* mainline ERE/ARE */
#define L_BRE	2				/* mainline BRE */
#define L_Q 3					/* REG_QUOTE */
#define L_EBND	4				/* ERE/ARE bound */
#define L_BBND	5				/* BRE bound */
#define L_BRACK 6				/* brackets */
#define L_CEL	7				/* collating element */
#define L_ECL	8				/* equivalence class */
#define L_CCL	9				/* character class */
#define INTOCON(c)	(v->lexcon = (c))
#define INCON(con)	(v->lexcon == (con))

/* construct pointer past end of chr array */
#define ENDOF(array)	((array) + sizeof(array)/sizeof(chr))

/*
 * lexstart - set up lexical stuff, scan leading options
 */
static void
lexstart(struct vars *v)
{
	prefixes(v);				/* may turn on new type bits etc. */
	NOERR();

	if (v->cflags & REG_QUOTE)
	{
		assert(!(v->cflags & (REG_ADVANCED | REG_EXPANDED | REG_NEWLINE)));
		INTOCON(L_Q);
	}
	else if (v->cflags & REG_EXTENDED)
	{
		assert(!(v->cflags & REG_QUOTE));
		INTOCON(L_ERE);
	}
	else
	{
		assert(!(v->cflags & (REG_QUOTE | REG_ADVF)));
		INTOCON(L_BRE);
	}

	v->nexttype = EMPTY;		/* remember we were at the start */
	next(v);					/* set up the first token */
}

/*
 * prefixes - implement various special prefixes
 */
static void
prefixes(struct vars *v)
{
	/* literal string doesn't get any of this stuff */
	if (v->cflags & REG_QUOTE)
		return;

	/* initial "***" gets special things */
	if (HAVE(4) && NEXT3('*', '*', '*'))
		switch (*(v->now + 3))
		{
			case CHR('?'):		/* "***?" error, msg shows version */
				ERR(REG_BADPAT);
				return;			/* proceed no further */
				break;
			case CHR('='):		/* "***=" shifts to literal string */
				NOTE(REG_UNONPOSIX);
				v->cflags |= REG_QUOTE;
				v->cflags &= ~(REG_ADVANCED | REG_EXPANDED | REG_NEWLINE);
				v->now += 4;
				return;			/* and there can be no more prefixes */
				break;
			case CHR(':'):		/* "***:" shifts to AREs */
				NOTE(REG_UNONPOSIX);
				v->cflags |= REG_ADVANCED;
				v->now += 4;
				break;
			default:			/* otherwise *** is just an error */
				ERR(REG_BADRPT);
				return;
				break;
		}

	/* BREs and EREs don't get embedded options */
	if ((v->cflags & REG_ADVANCED) != REG_ADVANCED)
		return;

	/* embedded options (AREs only) */
	if (HAVE(3) && NEXT2('(', '?') && iscalpha(*(v->now + 2)))
	{
		NOTE(REG_UNONPOSIX);
		v->now += 2;
		for (; !ATEOS() && iscalpha(*v->now); v->now++)
			switch (*v->now)
			{
				case CHR('b'):	/* BREs (but why???) */
					v->cflags &= ~(REG_ADVANCED | REG_QUOTE);
					break;
				case CHR('c'):	/* case sensitive */
					v->cflags &= ~REG_ICASE;
					break;
				case CHR('e'):	/* plain EREs */
					v->cflags |= REG_EXTENDED;
					v->cflags &= ~(REG_ADVF | REG_QUOTE);
					break;
				case CHR('i'):	/* case insensitive */
					v->cflags |= REG_ICASE;
					break;
				case CHR('m'):	/* Perloid synonym for n */
				case CHR('n'):	/* \n affects ^ $ . [^ */
					v->cflags |= REG_NEWLINE;
					break;
				case CHR('p'):	/* ~Perl, \n affects . [^ */
					v->cflags |= REG_NLSTOP;
					v->cflags &= ~REG_NLANCH;
					break;
				case CHR('q'):	/* literal string */
					v->cflags |= REG_QUOTE;
					v->cflags &= ~REG_ADVANCED;
					break;
				case CHR('s'):	/* single line, \n ordinary */
					v->cflags &= ~REG_NEWLINE;
					break;
				case CHR('t'):	/* tight syntax */
					v->cflags &= ~REG_EXPANDED;
					break;
				case CHR('w'):	/* weird, \n affects ^ $ only */
					v->cflags &= ~REG_NLSTOP;
					v->cflags |= REG_NLANCH;
					break;
				case CHR('x'):	/* expanded syntax */
					v->cflags |= REG_EXPANDED;
					break;
				default:
					ERR(REG_BADOPT);
					return;
			}
		if (!NEXT1(')'))
		{
			ERR(REG_BADOPT);
			return;
		}
		v->now++;
		if (v->cflags & REG_QUOTE)
			v->cflags &= ~(REG_EXPANDED | REG_NEWLINE);
	}
}

/*
 * next - get next token
 */
static int						/* 1 normal, 0 failure */
next(struct vars *v)
{
	chr			c;

next_restart:					/* loop here after eating a comment */

	/* errors yield an infinite sequence of failures */
	if (ISERR())
		return 0;				/* the error has set nexttype to EOS */

	/* remember flavor of last token */
	v->lasttype = v->nexttype;

	/* REG_BOSONLY */
	if (v->nexttype == EMPTY && (v->cflags & REG_BOSONLY))
	{
		/* at start of a REG_BOSONLY RE */
		RETV(SBEGIN, 0);		/* same as \A */
	}

	/* skip white space etc. if appropriate (not in literal or []) */
	if (v->cflags & REG_EXPANDED)
		switch (v->lexcon)
		{
			case L_ERE:
			case L_BRE:
			case L_EBND:
			case L_BBND:
				skip(v);
				break;
		}

	/* handle EOS, depending on context */
	if (ATEOS())
	{
		switch (v->lexcon)
		{
			case L_ERE:
			case L_BRE:
			case L_Q:
				RET(EOS);
				break;
			case L_EBND:
			case L_BBND:
				FAILW(REG_EBRACE);
				break;
			case L_BRACK:
			case L_CEL:
			case L_ECL:
			case L_CCL:
				FAILW(REG_EBRACK);
				break;
		}
		assert(NOTREACHED);
	}

	/* okay, time to actually get a character */
	c = *v->now++;

	/* deal with the easy contexts, punt EREs to code below */
	switch (v->lexcon)
	{
		case L_BRE:				/* punt BREs to separate function */
			return brenext(v, c);
			break;
		case L_ERE:				/* see below */
			break;
		case L_Q:				/* literal strings are easy */
			RETV(PLAIN, c);
			break;
		case L_BBND:			/* bounds are fairly simple */
		case L_EBND:
			switch (c)
			{
				case CHR('0'):
				case CHR('1'):
				case CHR('2'):
				case CHR('3'):
				case CHR('4'):
				case CHR('5'):
				case CHR('6'):
				case CHR('7'):
				case CHR('8'):
				case CHR('9'):
					RETV(DIGIT, (chr) DIGITVAL(c));
					break;
				case CHR(','):
					RET(',');
					break;
				case CHR('}'):	/* ERE bound ends with } */
					if (INCON(L_EBND))
					{
						INTOCON(L_ERE);
						if ((v->cflags & REG_ADVF) && NEXT1('?'))
						{
							v->now++;
							NOTE(REG_UNONPOSIX);
							RETV('}', 0);
						}
						RETV('}', 1);
					}
					else
						FAILW(REG_BADBR);
					break;
				case CHR('\\'): /* BRE bound ends with \} */
					if (INCON(L_BBND) && NEXT1('}'))
					{
						v->now++;
						INTOCON(L_BRE);
						RETV('}', 1);
					}
					else
						FAILW(REG_BADBR);
					break;
				default:
					FAILW(REG_BADBR);
					break;
			}
			assert(NOTREACHED);
			break;
		case L_BRACK:			/* brackets are not too hard */
			switch (c)
			{
				case CHR(']'):
					if (LASTTYPE('['))
						RETV(PLAIN, c);
					else
					{
						INTOCON((v->cflags & REG_EXTENDED) ?
								L_ERE : L_BRE);
						RET(']');
					}
					break;
				case CHR('\\'):
					NOTE(REG_UBBS);
					if (!(v->cflags & REG_ADVF))
						RETV(PLAIN, c);
					NOTE(REG_UNONPOSIX);
					if (ATEOS())
						FAILW(REG_EESCAPE);
					if (!lexescape(v))
						return 0;
					switch (v->nexttype)
					{			/* not all escapes okay here */
						case PLAIN:
						case CCLASSS:
						case CCLASSC:
							return 1;
							break;
					}
					/* not one of the acceptable escapes */
					FAILW(REG_EESCAPE);
					break;
				case CHR('-'):
					if (LASTTYPE('[') || NEXT1(']'))
						RETV(PLAIN, c);
					else
						RETV(RANGE, c);
					break;
				case CHR('['):
					if (ATEOS())
						FAILW(REG_EBRACK);
					switch (*v->now++)
					{
						case CHR('.'):
							INTOCON(L_CEL);
							/* might or might not be locale-specific */
							RET(COLLEL);
							break;
						case CHR('='):
							INTOCON(L_ECL);
							NOTE(REG_ULOCALE);
							RET(ECLASS);
							break;
						case CHR(':'):
							INTOCON(L_CCL);
							NOTE(REG_ULOCALE);
							RET(CCLASS);
							break;
						default:	/* oops */
							v->now--;
							RETV(PLAIN, c);
							break;
					}
					assert(NOTREACHED);
					break;
				default:
					RETV(PLAIN, c);
					break;
			}
			assert(NOTREACHED);
			break;
		case L_CEL:				/* collating elements are easy */
			if (c == CHR('.') && NEXT1(']'))
			{
				v->now++;
				INTOCON(L_BRACK);
				RETV(END, '.');
			}
			else
				RETV(PLAIN, c);
			break;
		case L_ECL:				/* ditto equivalence classes */
			if (c == CHR('=') && NEXT1(']'))
			{
				v->now++;
				INTOCON(L_BRACK);
				RETV(END, '=');
			}
			else
				RETV(PLAIN, c);
			break;
		case L_CCL:				/* ditto character classes */
			if (c == CHR(':') && NEXT1(']'))
			{
				v->now++;
				INTOCON(L_BRACK);
				RETV(END, ':');
			}
			else
				RETV(PLAIN, c);
			break;
		default:
			assert(NOTREACHED);
			break;
	}

	/* that got rid of everything except EREs and AREs */
	assert(INCON(L_ERE));

	/* deal with EREs and AREs, except for backslashes */
	switch (c)
	{
		case CHR('|'):
			RET('|');
			break;
		case CHR('*'):
			if ((v->cflags & REG_ADVF) && NEXT1('?'))
			{
				v->now++;
				NOTE(REG_UNONPOSIX);
				RETV('*', 0);
			}
			RETV('*', 1);
			break;
		case CHR('+'):
			if ((v->cflags & REG_ADVF) && NEXT1('?'))
			{
				v->now++;
				NOTE(REG_UNONPOSIX);
				RETV('+', 0);
			}
			RETV('+', 1);
			break;
		case CHR('?'):
			if ((v->cflags & REG_ADVF) && NEXT1('?'))
			{
				v->now++;
				NOTE(REG_UNONPOSIX);
				RETV('?', 0);
			}
			RETV('?', 1);
			break;
		case CHR('{'):			/* bounds start or plain character */
			if (v->cflags & REG_EXPANDED)
				skip(v);
			if (ATEOS() || !iscdigit(*v->now))
			{
				NOTE(REG_UBRACES);
				NOTE(REG_UUNSPEC);
				RETV(PLAIN, c);
			}
			else
			{
				NOTE(REG_UBOUNDS);
				INTOCON(L_EBND);
				RET('{');
			}
			assert(NOTREACHED);
			break;
		case CHR('('):			/* parenthesis, or advanced extension */
			if ((v->cflags & REG_ADVF) && NEXT1('?'))
			{
				NOTE(REG_UNONPOSIX);
				v->now++;
				if (ATEOS())
					FAILW(REG_BADRPT);
				switch (*v->now++)
				{
					case CHR(':'):	/* non-capturing paren */
						RETV('(', 0);
						break;
					case CHR('#'):	/* comment */
						while (!ATEOS() && *v->now != CHR(')'))
							v->now++;
						if (!ATEOS())
							v->now++;
						assert(v->nexttype == v->lasttype);
						goto next_restart;
					case CHR('='):	/* positive lookahead */
						NOTE(REG_ULOOKAROUND);
						RETV(LACON, LATYPE_AHEAD_POS);
						break;
					case CHR('!'):	/* negative lookahead */
						NOTE(REG_ULOOKAROUND);
						RETV(LACON, LATYPE_AHEAD_NEG);
						break;
					case CHR('<'):
						if (ATEOS())
							FAILW(REG_BADRPT);
						switch (*v->now++)
						{
							case CHR('='):	/* positive lookbehind */
								NOTE(REG_ULOOKAROUND);
								RETV(LACON, LATYPE_BEHIND_POS);
								break;
							case CHR('!'):	/* negative lookbehind */
								NOTE(REG_ULOOKAROUND);
								RETV(LACON, LATYPE_BEHIND_NEG);
								break;
							default:
								FAILW(REG_BADRPT);
								break;
						}
						assert(NOTREACHED);
						break;
					default:
						FAILW(REG_BADRPT);
						break;
				}
				assert(NOTREACHED);
			}
			RETV('(', 1);
			break;
		case CHR(')'):
			if (LASTTYPE('('))
				NOTE(REG_UUNSPEC);
			RETV(')', c);
			break;
		case CHR('['):			/* easy except for [[:<:]] and [[:>:]] */
			if (HAVE(6) && *(v->now + 0) == CHR('[') &&
				*(v->now + 1) == CHR(':') &&
				(*(v->now + 2) == CHR('<') ||
				 *(v->now + 2) == CHR('>')) &&
				*(v->now + 3) == CHR(':') &&
				*(v->now + 4) == CHR(']') &&
				*(v->now + 5) == CHR(']'))
			{
				c = *(v->now + 2);
				v->now += 6;
				NOTE(REG_UNONPOSIX);
				RET((c == CHR('<')) ? '<' : '>');
			}
			INTOCON(L_BRACK);
			if (NEXT1('^'))
			{
				v->now++;
				RETV('[', 0);
			}
			RETV('[', 1);
			break;
		case CHR('.'):
			RET('.');
			break;
		case CHR('^'):
			RET('^');
			break;
		case CHR('$'):
			RET('$');
			break;
		case CHR('\\'):			/* mostly punt backslashes to code below */
			if (ATEOS())
				FAILW(REG_EESCAPE);
			break;
		default:				/* ordinary character */
			RETV(PLAIN, c);
			break;
	}

	/* ERE/ARE backslash handling; backslash already eaten */
	assert(!ATEOS());
	if (!(v->cflags & REG_ADVF))
	{							/* only AREs have non-trivial escapes */
		if (iscalnum(*v->now))
		{
			NOTE(REG_UBSALNUM);
			NOTE(REG_UUNSPEC);
		}
		RETV(PLAIN, *v->now++);
	}
	return lexescape(v);
}

/*
 * lexescape - parse an ARE backslash escape (backslash already eaten)
 *
 * This is used for ARE backslashes both normally and inside bracket
 * expressions.  In the latter case, not all escape types are allowed,
 * but the caller must reject unwanted ones after we return.
 */
static int
lexescape(struct vars *v)
{
	chr			c;
	static const chr alert[] = {
		CHR('a'), CHR('l'), CHR('e'), CHR('r'), CHR('t')
	};
	static const chr esc[] = {
		CHR('E'), CHR('S'), CHR('C')
	};
	const chr  *save;

	assert(v->cflags & REG_ADVF);

	assert(!ATEOS());
	c = *v->now++;
	if (!iscalnum(c))
		RETV(PLAIN, c);

	NOTE(REG_UNONPOSIX);
	switch (c)
	{
		case CHR('a'):
			RETV(PLAIN, chrnamed(v, alert, ENDOF(alert), CHR('\007')));
			break;
		case CHR('A'):
			RETV(SBEGIN, 0);
			break;
		case CHR('b'):
			RETV(PLAIN, CHR('\b'));
			break;
		case CHR('B'):
			RETV(PLAIN, CHR('\\'));
			break;
		case CHR('c'):
			NOTE(REG_UUNPORT);
			if (ATEOS())
				FAILW(REG_EESCAPE);
			RETV(PLAIN, (chr) (*v->now++ & 037));
			break;
		case CHR('d'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSS, CC_DIGIT);
			break;
		case CHR('D'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSC, CC_DIGIT);
			break;
		case CHR('e'):
			NOTE(REG_UUNPORT);
			RETV(PLAIN, chrnamed(v, esc, ENDOF(esc), CHR('\033')));
			break;
		case CHR('f'):
			RETV(PLAIN, CHR('\f'));
			break;
		case CHR('m'):
			RET('<');
			break;
		case CHR('M'):
			RET('>');
			break;
		case CHR('n'):
			RETV(PLAIN, CHR('\n'));
			break;
		case CHR('r'):
			RETV(PLAIN, CHR('\r'));
			break;
		case CHR('s'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSS, CC_SPACE);
			break;
		case CHR('S'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSC, CC_SPACE);
			break;
		case CHR('t'):
			RETV(PLAIN, CHR('\t'));
			break;
		case CHR('u'):
			c = lexdigits(v, 16, 4, 4);
			if (ISERR() || !CHR_IS_IN_RANGE(c))
				FAILW(REG_EESCAPE);
			RETV(PLAIN, c);
			break;
		case CHR('U'):
			c = lexdigits(v, 16, 8, 8);
			if (ISERR() || !CHR_IS_IN_RANGE(c))
				FAILW(REG_EESCAPE);
			RETV(PLAIN, c);
			break;
		case CHR('v'):
			RETV(PLAIN, CHR('\v'));
			break;
		case CHR('w'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSS, CC_WORD);
			break;
		case CHR('W'):
			NOTE(REG_ULOCALE);
			RETV(CCLASSC, CC_WORD);
			break;
		case CHR('x'):
			NOTE(REG_UUNPORT);
			c = lexdigits(v, 16, 1, 255);	/* REs >255 long outside spec */
			if (ISERR() || !CHR_IS_IN_RANGE(c))
				FAILW(REG_EESCAPE);
			RETV(PLAIN, c);
			break;
		case CHR('y'):
			NOTE(REG_ULOCALE);
			RETV(WBDRY, 0);
			break;
		case CHR('Y'):
			NOTE(REG_ULOCALE);
			RETV(NWBDRY, 0);
			break;
		case CHR('Z'):
			RETV(SEND, 0);
			break;
		case CHR('1'):
		case CHR('2'):
		case CHR('3'):
		case CHR('4'):
		case CHR('5'):
		case CHR('6'):
		case CHR('7'):
		case CHR('8'):
		case CHR('9'):
			save = v->now;
			v->now--;			/* put first digit back */
			c = lexdigits(v, 10, 1, 255);	/* REs >255 long outside spec */
			if (ISERR())
				FAILW(REG_EESCAPE);
			/* ugly heuristic (first test is "exactly 1 digit?") */
			if (v->now == save || ((int) c > 0 && (int) c <= v->nsubexp))
			{
				NOTE(REG_UBACKREF);
				RETV(BACKREF, c);
			}
			/* oops, doesn't look like it's a backref after all... */
			v->now = save;
			/* and fall through into octal number */
			switch_fallthrough();
		case CHR('0'):
			NOTE(REG_UUNPORT);
			v->now--;			/* put first digit back */
			c = lexdigits(v, 8, 1, 3);
			if (ISERR())
				FAILW(REG_EESCAPE);
			if (c > 0xff)
			{
				/* out of range, so we handled one digit too much */
				v->now--;
				c >>= 3;
			}
			RETV(PLAIN, c);
			break;
		default:
			assert(iscalpha(c));
			FAILW(REG_EESCAPE); /* unknown alphabetic escape */
			break;
	}
	assert(NOTREACHED);
}

/*
 * lexdigits - slurp up digits and return chr value
 *
 * This does not account for overflow; callers should range-check the result
 * if maxlen is large enough to make that possible.
 */
static chr						/* chr value; errors signalled via ERR */
lexdigits(struct vars *v,
		  int base,
		  int minlen,
		  int maxlen)
{
	uchr		n;				/* unsigned to avoid overflow misbehavior */
	int			len;
	chr			c;
	int			d;
	const uchr	ub = (uchr) base;

	n = 0;
	for (len = 0; len < maxlen && !ATEOS(); len++)
	{
		c = *v->now++;
		switch (c)
		{
			case CHR('0'):
			case CHR('1'):
			case CHR('2'):
			case CHR('3'):
			case CHR('4'):
			case CHR('5'):
			case CHR('6'):
			case CHR('7'):
			case CHR('8'):
			case CHR('9'):
				d = DIGITVAL(c);
				break;
			case CHR('a'):
			case CHR('A'):
				d = 10;
				break;
			case CHR('b'):
			case CHR('B'):
				d = 11;
				break;
			case CHR('c'):
			case CHR('C'):
				d = 12;
				break;
			case CHR('d'):
			case CHR('D'):
				d = 13;
				break;
			case CHR('e'):
			case CHR('E'):
				d = 14;
				break;
			case CHR('f'):
			case CHR('F'):
				d = 15;
				break;
			default:
				v->now--;		/* oops, not a digit at all */
				d = -1;
				break;
		}

		if (d >= base)
		{						/* not a plausible digit */
			v->now--;
			d = -1;
		}
		if (d < 0)
			break;				/* NOTE BREAK OUT */
		n = n * ub + (uchr) d;
	}
	if (len < minlen)
		ERR(REG_EESCAPE);

	return (chr) n;
}

/*
 * brenext - get next BRE token
 *
 * This is much like EREs except for all the stupid backslashes and the
 * context-dependency of some things.
 */
static int						/* 1 normal, 0 failure */
brenext(struct vars *v,
		chr c)
{
	switch (c)
	{
		case CHR('*'):
			if (LASTTYPE(EMPTY) || LASTTYPE('(') || LASTTYPE('^'))
				RETV(PLAIN, c);
			RETV('*', 1);
			break;
		case CHR('['):
			if (HAVE(6) && *(v->now + 0) == CHR('[') &&
				*(v->now + 1) == CHR(':') &&
				(*(v->now + 2) == CHR('<') ||
				 *(v->now + 2) == CHR('>')) &&
				*(v->now + 3) == CHR(':') &&
				*(v->now + 4) == CHR(']') &&
				*(v->now + 5) == CHR(']'))
			{
				c = *(v->now + 2);
				v->now += 6;
				NOTE(REG_UNONPOSIX);
				RET((c == CHR('<')) ? '<' : '>');
			}
			INTOCON(L_BRACK);
			if (NEXT1('^'))
			{
				v->now++;
				RETV('[', 0);
			}
			RETV('[', 1);
			break;
		case CHR('.'):
			RET('.');
			break;
		case CHR('^'):
			if (LASTTYPE(EMPTY))
				RET('^');
			if (LASTTYPE('('))
			{
				NOTE(REG_UUNSPEC);
				RET('^');
			}
			RETV(PLAIN, c);
			break;
		case CHR('$'):
			if (v->cflags & REG_EXPANDED)
				skip(v);
			if (ATEOS())
				RET('$');
			if (NEXT2('\\', ')'))
			{
				NOTE(REG_UUNSPEC);
				RET('$');
			}
			RETV(PLAIN, c);
			break;
		case CHR('\\'):
			break;				/* see below */
		default:
			RETV(PLAIN, c);
			break;
	}

	assert(c == CHR('\\'));

	if (ATEOS())
		FAILW(REG_EESCAPE);

	c = *v->now++;
	switch (c)
	{
		case CHR('{'):
			INTOCON(L_BBND);
			NOTE(REG_UBOUNDS);
			RET('{');
			break;
		case CHR('('):
			RETV('(', 1);
			break;
		case CHR(')'):
			RETV(')', c);
			break;
		case CHR('<'):
			NOTE(REG_UNONPOSIX);
			RET('<');
			break;
		case CHR('>'):
			NOTE(REG_UNONPOSIX);
			RET('>');
			break;
		case CHR('1'):
		case CHR('2'):
		case CHR('3'):
		case CHR('4'):
		case CHR('5'):
		case CHR('6'):
		case CHR('7'):
		case CHR('8'):
		case CHR('9'):
			NOTE(REG_UBACKREF);
			RETV(BACKREF, (chr) DIGITVAL(c));
			break;
		default:
			if (iscalnum(c))
			{
				NOTE(REG_UBSALNUM);
				NOTE(REG_UUNSPEC);
			}
			RETV(PLAIN, c);
			break;
	}

	assert(NOTREACHED);
	return 0;
}

/*
 * skip - skip white space and comments in expanded form
 */
static void
skip(struct vars *v)
{
	const chr  *start = v->now;

	assert(v->cflags & REG_EXPANDED);

	for (;;)
	{
		while (!ATEOS() && iscspace(*v->now))
			v->now++;
		if (ATEOS() || *v->now != CHR('#'))
			break;				/* NOTE BREAK OUT */
		assert(NEXT1('#'));
		while (!ATEOS() && *v->now != CHR('\n'))
			v->now++;
		/* leave the newline to be picked up by the iscspace loop */
	}

	if (v->now != start)
		NOTE(REG_UNONPOSIX);
}

/*
 * newline - return the chr for a newline
 *
 * This helps confine use of CHR to this source file.
 */
static chr
newline(void)
{
	return CHR('\n');
}

/*
 * chrnamed - return the chr known by a given (chr string) name
 *
 * The code is a bit clumsy, but this routine gets only such specialized
 * use that it hardly matters.
 */
static chr
chrnamed(struct vars *v,
		 const chr *startp,		/* start of name */
		 const chr *endp,		/* just past end of name */
		 chr lastresort)		/* what to return if name lookup fails */
{
	chr			c;
	int			errsave;
	int			e;
	struct cvec *cv;

	errsave = v->err;
	v->err = 0;
	c = element(v, startp, endp);
	e = v->err;
	v->err = errsave;

	if (e != 0)
		return lastresort;

	cv = range(v, c, c, 0);
	if (cv->nchrs == 0)
		return lastresort;
	return cv->chrs[0];
}

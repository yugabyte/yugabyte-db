/*
 * DFA routines
 * This file is #included by regexec.c.
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
 * src/backend/regex/rege_dfa.c
 *
 */

/*
 * longest - longest-preferred matching engine
 *
 * On success, returns match endpoint address.  Returns NULL on no match.
 * Internal errors also return NULL, with v->err set.
 */
static chr *
longest(struct vars *v,
		struct dfa *d,
		chr *start,				/* where the match should start */
		chr *stop,				/* match must end at or before here */
		int *hitstopp)			/* record whether hit v->stop, if non-NULL */
{
	chr		   *cp;
	chr		   *realstop = (stop == v->stop) ? stop : stop + 1;
	color		co;
	struct sset *css;
	struct sset *ss;
	chr		   *post;
	int			i;
	struct colormap *cm = d->cm;

	/* prevent "uninitialized variable" warnings */
	if (hitstopp != NULL)
		*hitstopp = 0;

	/* if this is a backref to a known string, just match against that */
	if (d->backno >= 0)
	{
		assert((size_t) d->backno < v->nmatch);
		if (v->pmatch[d->backno].rm_so >= 0)
		{
			cp = dfa_backref(v, d, start, start, stop, false);
			if (cp == v->stop && stop == v->stop && hitstopp != NULL)
				*hitstopp = 1;
			return cp;
		}
	}

	/* fast path for matchall NFAs */
	if (d->cnfa->flags & MATCHALL)
	{
		size_t		nchr = stop - start;
		size_t		maxmatchall = d->cnfa->maxmatchall;

		if (nchr < d->cnfa->minmatchall)
			return NULL;
		if (maxmatchall == DUPINF)
		{
			if (stop == v->stop && hitstopp != NULL)
				*hitstopp = 1;
		}
		else
		{
			if (stop == v->stop && nchr <= maxmatchall + 1 && hitstopp != NULL)
				*hitstopp = 1;
			if (nchr > maxmatchall)
				return start + maxmatchall;
		}
		return stop;
	}

	/* initialize */
	css = initialize(v, d, start);
	if (css == NULL)
		return NULL;
	cp = start;

	/* startup */
	FDEBUG(("+++ startup +++\n"));
	if (cp == v->start)
	{
		co = d->cnfa->bos[(v->eflags & REG_NOTBOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));
	}
	else
	{
		co = GETCOLOR(cm, *(cp - 1));
		FDEBUG(("char %c, color %ld\n", (char) *(cp - 1), (long) co));
	}
	css = miss(v, d, css, co, cp, start);
	if (css == NULL)
		return NULL;
	css->lastseen = cp;

	/*
	 * This is the main text-scanning loop.  It seems worth having two copies
	 * to avoid the overhead of REG_FTRACE tests here, even in REG_DEBUG
	 * builds, when you're not actively tracing.
	 */
#ifdef REG_DEBUG
	if (v->eflags & REG_FTRACE)
	{
		while (cp < realstop)
		{
			FDEBUG(("+++ at c%d +++\n", (int) (css - d->ssets)));
			co = GETCOLOR(cm, *cp);
			FDEBUG(("char %c, color %ld\n", (char) *cp, (long) co));
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
		}
	}
	else
#endif
	{
		while (cp < realstop)
		{
			co = GETCOLOR(cm, *cp);
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
		}
	}

	if (ISERR())
		return NULL;

	/* shutdown */
	FDEBUG(("+++ shutdown at c%d +++\n", (int) (css - d->ssets)));
	if (cp == v->stop && stop == v->stop)
	{
		if (hitstopp != NULL)
			*hitstopp = 1;
		co = d->cnfa->eos[(v->eflags & REG_NOTEOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));
		ss = miss(v, d, css, co, cp, start);
		if (ISERR())
			return NULL;
		/* special case:  match ended at eol? */
		if (ss != NULL && (ss->flags & POSTSTATE))
			return cp;
		else if (ss != NULL)
			ss->lastseen = cp;	/* to be tidy */
	}

	/* find last match, if any */
	post = d->lastpost;
	for (ss = d->ssets, i = d->nssused; i > 0; ss++, i--)
		if ((ss->flags & POSTSTATE) && post != ss->lastseen &&
			(post == NULL || post < ss->lastseen))
			post = ss->lastseen;
	if (post != NULL)			/* found one */
		return post - 1;

	return NULL;
}

/*
 * shortest - shortest-preferred matching engine
 *
 * On success, returns match endpoint address.  Returns NULL on no match.
 * Internal errors also return NULL, with v->err set.
 */
static chr *
shortest(struct vars *v,
		 struct dfa *d,
		 chr *start,			/* where the match should start */
		 chr *min,				/* match must end at or after here */
		 chr *max,				/* match must end at or before here */
		 chr **coldp,			/* store coldstart pointer here, if non-NULL */
		 int *hitstopp)			/* record whether hit v->stop, if non-NULL */
{
	chr		   *cp;
	chr		   *realmin = (min == v->stop) ? min : min + 1;
	chr		   *realmax = (max == v->stop) ? max : max + 1;
	color		co;
	struct sset *css;
	struct sset *ss;
	struct colormap *cm = d->cm;

	/* prevent "uninitialized variable" warnings */
	if (coldp != NULL)
		*coldp = NULL;
	if (hitstopp != NULL)
		*hitstopp = 0;

	/* if this is a backref to a known string, just match against that */
	if (d->backno >= 0)
	{
		assert((size_t) d->backno < v->nmatch);
		if (v->pmatch[d->backno].rm_so >= 0)
		{
			cp = dfa_backref(v, d, start, min, max, true);
			if (cp != NULL && coldp != NULL)
				*coldp = start;
			/* there is no case where we should set *hitstopp */
			return cp;
		}
	}

	/* fast path for matchall NFAs */
	if (d->cnfa->flags & MATCHALL)
	{
		size_t		nchr = min - start;

		if (d->cnfa->maxmatchall != DUPINF &&
			nchr > d->cnfa->maxmatchall)
			return NULL;
		if ((max - start) < d->cnfa->minmatchall)
			return NULL;
		if (nchr < d->cnfa->minmatchall)
			min = start + d->cnfa->minmatchall;
		if (coldp != NULL)
			*coldp = start;
		/* there is no case where we should set *hitstopp */
		return min;
	}

	/* initialize */
	css = initialize(v, d, start);
	if (css == NULL)
		return NULL;
	cp = start;

	/* startup */
	FDEBUG(("--- startup ---\n"));
	if (cp == v->start)
	{
		co = d->cnfa->bos[(v->eflags & REG_NOTBOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));
	}
	else
	{
		co = GETCOLOR(cm, *(cp - 1));
		FDEBUG(("char %c, color %ld\n", (char) *(cp - 1), (long) co));
	}
	css = miss(v, d, css, co, cp, start);
	if (css == NULL)
		return NULL;
	css->lastseen = cp;
	ss = css;

	/*
	 * This is the main text-scanning loop.  It seems worth having two copies
	 * to avoid the overhead of REG_FTRACE tests here, even in REG_DEBUG
	 * builds, when you're not actively tracing.
	 */
#ifdef REG_DEBUG
	if (v->eflags & REG_FTRACE)
	{
		while (cp < realmax)
		{
			FDEBUG(("--- at c%d ---\n", (int) (css - d->ssets)));
			co = GETCOLOR(cm, *cp);
			FDEBUG(("char %c, color %ld\n", (char) *cp, (long) co));
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
			if ((ss->flags & POSTSTATE) && cp >= realmin)
				break;			/* NOTE BREAK OUT */
		}
	}
	else
#endif
	{
		while (cp < realmax)
		{
			co = GETCOLOR(cm, *cp);
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
			if ((ss->flags & POSTSTATE) && cp >= realmin)
				break;			/* NOTE BREAK OUT */
		}
	}

	if (ss == NULL)
		return NULL;

	if (coldp != NULL)			/* report last no-progress state set, if any */
		*coldp = lastcold(v, d);

	if ((ss->flags & POSTSTATE) && cp > min)
	{
		assert(cp >= realmin);
		cp--;
	}
	else if (cp == v->stop && max == v->stop)
	{
		co = d->cnfa->eos[(v->eflags & REG_NOTEOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));
		ss = miss(v, d, css, co, cp, start);
		/* match might have ended at eol */
		if ((ss == NULL || !(ss->flags & POSTSTATE)) && hitstopp != NULL)
			*hitstopp = 1;
	}

	if (ss == NULL || !(ss->flags & POSTSTATE))
		return NULL;

	return cp;
}

/*
 * matchuntil - incremental matching engine
 *
 * This is meant for use with a search-style NFA (that is, the pattern is
 * known to act as though it had a leading .*).  We determine whether a
 * match exists starting at v->start and ending at probe.  Multiple calls
 * require only O(N) time not O(N^2) so long as the probe values are
 * nondecreasing.  *lastcss and *lastcp must be initialized to NULL before
 * starting a series of calls.
 *
 * Returns 1 if a match exists, 0 if not.
 * Internal errors also return 0, with v->err set.
 */
static int
matchuntil(struct vars *v,
		   struct dfa *d,
		   chr *probe,			/* we want to know if a match ends here */
		   struct sset **lastcss,	/* state storage across calls */
		   chr **lastcp)		/* state storage across calls */
{
	chr		   *cp = *lastcp;
	color		co;
	struct sset *css = *lastcss;
	struct sset *ss;
	struct colormap *cm = d->cm;

	/* fast path for matchall NFAs */
	if (d->cnfa->flags & MATCHALL)
	{
		size_t		nchr = probe - v->start;

		if (nchr < d->cnfa->minmatchall)
			return 0;
		/* maxmatchall will always be infinity, cf. makesearch() */
		assert(d->cnfa->maxmatchall == DUPINF);
		return 1;
	}

	/* initialize and startup, or restart, if necessary */
	if (cp == NULL || cp > probe)
	{
		cp = v->start;
		css = initialize(v, d, cp);
		if (css == NULL)
			return 0;

		FDEBUG((">>> startup >>>\n"));
		co = d->cnfa->bos[(v->eflags & REG_NOTBOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));

		css = miss(v, d, css, co, cp, v->start);
		if (css == NULL)
			return 0;
		css->lastseen = cp;
	}
	else if (css == NULL)
	{
		/* we previously found that no match is possible beyond *lastcp */
		return 0;
	}
	ss = css;

	/*
	 * This is the main text-scanning loop.  It seems worth having two copies
	 * to avoid the overhead of REG_FTRACE tests here, even in REG_DEBUG
	 * builds, when you're not actively tracing.
	 */
#ifdef REG_DEBUG
	if (v->eflags & REG_FTRACE)
	{
		while (cp < probe)
		{
			FDEBUG((">>> at c%d >>>\n", (int) (css - d->ssets)));
			co = GETCOLOR(cm, *cp);
			FDEBUG(("char %c, color %ld\n", (char) *cp, (long) co));
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, v->start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
		}
	}
	else
#endif
	{
		while (cp < probe)
		{
			co = GETCOLOR(cm, *cp);
			ss = css->outs[co];
			if (ss == NULL)
			{
				ss = miss(v, d, css, co, cp + 1, v->start);
				if (ss == NULL)
					break;		/* NOTE BREAK OUT */
			}
			cp++;
			ss->lastseen = cp;
			css = ss;
		}
	}

	*lastcss = ss;
	*lastcp = cp;

	if (ss == NULL)
		return 0;				/* impossible match, or internal error */

	/* We need to process one more chr, or the EOS symbol, to check match */
	if (cp < v->stop)
	{
		FDEBUG((">>> at c%d >>>\n", (int) (css - d->ssets)));
		co = GETCOLOR(cm, *cp);
		FDEBUG(("char %c, color %ld\n", (char) *cp, (long) co));
		ss = css->outs[co];
		if (ss == NULL)
			ss = miss(v, d, css, co, cp + 1, v->start);
	}
	else
	{
		assert(cp == v->stop);
		co = d->cnfa->eos[(v->eflags & REG_NOTEOL) ? 0 : 1];
		FDEBUG(("color %ld\n", (long) co));
		ss = miss(v, d, css, co, cp, v->start);
	}

	if (ss == NULL || !(ss->flags & POSTSTATE))
		return 0;

	return 1;
}

/*
 * dfa_backref - find best match length for a known backref string
 *
 * When the backref's referent is already available, we can deliver an exact
 * answer with considerably less work than running the backref node's NFA.
 *
 * Return match endpoint for longest or shortest valid repeated match,
 * or NULL if there is no valid match.
 *
 * Should be in sync with cbrdissect(), although that has the different task
 * of checking a match to a predetermined section of the string.
 */
static chr *
dfa_backref(struct vars *v,
			struct dfa *d,
			chr *start,			/* where the match should start */
			chr *min,			/* match must end at or after here */
			chr *max,			/* match must end at or before here */
			bool shortest)
{
	int			n = d->backno;
	int			backmin = d->backmin;
	int			backmax = d->backmax;
	size_t		numreps;
	size_t		minreps;
	size_t		maxreps;
	size_t		brlen;
	chr		   *brstring;
	chr		   *p;

	/* get the backreferenced string (caller should have checked this) */
	if (v->pmatch[n].rm_so == -1)
		return NULL;
	brstring = v->start + v->pmatch[n].rm_so;
	brlen = v->pmatch[n].rm_eo - v->pmatch[n].rm_so;

	/* special-case zero-length backreference to avoid divide by zero */
	if (brlen == 0)
	{
		/*
		 * matches only a zero-length string, but any number of repetitions
		 * can be considered to be present
		 */
		if (min == start && backmin <= backmax)
			return start;
		return NULL;
	}

	/*
	 * convert min and max into numbers of possible repetitions of the backref
	 * string, rounding appropriately
	 */
	if (min <= start)
		minreps = 0;
	else
		minreps = (min - start - 1) / brlen + 1;
	maxreps = (max - start) / brlen;

	/* apply bounds, then see if there is any allowed match length */
	if (minreps < backmin)
		minreps = backmin;
	if (backmax != DUPINF && maxreps > backmax)
		maxreps = backmax;
	if (maxreps < minreps)
		return NULL;

	/* quick exit if zero-repetitions match is valid and preferred */
	if (shortest && minreps == 0)
		return start;

	/* okay, compare the actual string contents */
	p = start;
	numreps = 0;
	while (numreps < maxreps)
	{
		if ((*v->g->compare) (brstring, p, brlen) != 0)
			break;
		p += brlen;
		numreps++;
		if (shortest && numreps >= minreps)
			break;
	}

	if (numreps >= minreps)
		return p;
	return NULL;
}

/*
 * lastcold - determine last point at which no progress had been made
 */
static chr *					/* endpoint, or NULL */
lastcold(struct vars *v,
		 struct dfa *d)
{
	struct sset *ss;
	chr		   *nopr;
	int			i;

	nopr = d->lastnopr;
	if (nopr == NULL)
		nopr = v->start;
	for (ss = d->ssets, i = d->nssused; i > 0; ss++, i--)
		if ((ss->flags & NOPROGRESS) && nopr < ss->lastseen)
			nopr = ss->lastseen;
	return nopr;
}

/*
 * newdfa - set up a fresh DFA
 *
 * Returns NULL (and sets v->err) on failure.
 */
static struct dfa *
newdfa(struct vars *v,
	   struct cnfa *cnfa,
	   struct colormap *cm,
	   struct smalldfa *sml)	/* preallocated space, may be NULL */
{
	struct dfa *d;
	size_t		nss = cnfa->nstates * 2;
	int			wordsper = (cnfa->nstates + UBITS - 1) / UBITS;
	bool		ismalloced = false;

	assert(cnfa != NULL && cnfa->nstates != 0);

	if (nss <= FEWSTATES && cnfa->ncolors <= FEWCOLORS)
	{
		assert(wordsper == 1);
		if (sml == NULL)
		{
			sml = (struct smalldfa *) MALLOC(sizeof(struct smalldfa));
			if (sml == NULL)
			{
				ERR(REG_ESPACE);
				return NULL;
			}
			ismalloced = true;
		}
		d = &sml->dfa;
		d->ssets = sml->ssets;
		d->statesarea = sml->statesarea;
		d->work = &d->statesarea[nss];
		d->outsarea = sml->outsarea;
		d->incarea = sml->incarea;
		d->ismalloced = ismalloced;
		d->arraysmalloced = false;	/* not separately allocated, anyway */
	}
	else
	{
		d = (struct dfa *) MALLOC(sizeof(struct dfa));
		if (d == NULL)
		{
			ERR(REG_ESPACE);
			return NULL;
		}
		d->ssets = (struct sset *) MALLOC(nss * sizeof(struct sset));
		d->statesarea = (unsigned *) MALLOC((nss + WORK) * wordsper *
											sizeof(unsigned));
		d->work = &d->statesarea[nss * wordsper];
		d->outsarea = (struct sset **) MALLOC(nss * cnfa->ncolors *
											  sizeof(struct sset *));
		d->incarea = (struct arcp *) MALLOC(nss * cnfa->ncolors *
											sizeof(struct arcp));
		d->ismalloced = true;
		d->arraysmalloced = true;
		/* now freedfa() will behave sanely */
		if (d->ssets == NULL || d->statesarea == NULL ||
			d->outsarea == NULL || d->incarea == NULL)
		{
			freedfa(d);
			ERR(REG_ESPACE);
			return NULL;
		}
	}

	d->nssets = (v->eflags & REG_SMALL) ? 7 : nss;
	d->nssused = 0;
	d->nstates = cnfa->nstates;
	d->ncolors = cnfa->ncolors;
	d->wordsper = wordsper;
	d->cnfa = cnfa;
	d->cm = cm;
	d->lastpost = NULL;
	d->lastnopr = NULL;
	d->search = d->ssets;
	d->backno = -1;				/* may be set by caller */
	d->backmin = d->backmax = 0;

	/* initialization of sset fields is done as needed */

	return d;
}

/*
 * freedfa - free a DFA
 */
static void
freedfa(struct dfa *d)
{
	if (d->arraysmalloced)
	{
		if (d->ssets != NULL)
			FREE(d->ssets);
		if (d->statesarea != NULL)
			FREE(d->statesarea);
		if (d->outsarea != NULL)
			FREE(d->outsarea);
		if (d->incarea != NULL)
			FREE(d->incarea);
	}

	if (d->ismalloced)
		FREE(d);
}

/*
 * hash - construct a hash code for a bitvector
 *
 * There are probably better ways, but they're more expensive.
 */
static unsigned
hash(unsigned *uv,
	 int n)
{
	int			i;
	unsigned	h;

	h = 0;
	for (i = 0; i < n; i++)
		h ^= uv[i];
	return h;
}

/*
 * initialize - hand-craft a cache entry for startup, otherwise get ready
 */
static struct sset *
initialize(struct vars *v,
		   struct dfa *d,
		   chr *start)
{
	struct sset *ss;
	int			i;

	/* is previous one still there? */
	if (d->nssused > 0 && (d->ssets[0].flags & STARTER))
		ss = &d->ssets[0];
	else
	{							/* no, must (re)build it */
		ss = getvacant(v, d, start, start);
		if (ss == NULL)
			return NULL;
		for (i = 0; i < d->wordsper; i++)
			ss->states[i] = 0;
		BSET(ss->states, d->cnfa->pre);
		ss->hash = HASH(ss->states, d->wordsper);
		assert(d->cnfa->pre != d->cnfa->post);
		ss->flags = STARTER | LOCKED | NOPROGRESS;
		/* lastseen dealt with below */
	}

	for (i = 0; i < d->nssused; i++)
		d->ssets[i].lastseen = NULL;
	ss->lastseen = start;		/* maybe untrue, but harmless */
	d->lastpost = NULL;
	d->lastnopr = NULL;
	return ss;
}

/*
 * miss - handle a stateset cache miss
 *
 * css is the current stateset, co is the color of the current input character,
 * cp points to the character after that (which is where we may need to test
 * LACONs).  start does not affect matching behavior but is needed for pickss'
 * heuristics about which stateset cache entry to replace.
 *
 * Ordinarily, returns the address of the next stateset (the one that is
 * valid after consuming the input character).  Returns NULL if no valid
 * NFA states remain, ie we have a certain match failure.
 * Internal errors also return NULL, with v->err set.
 */
static struct sset *
miss(struct vars *v,
	 struct dfa *d,
	 struct sset *css,
	 color co,
	 chr *cp,					/* next chr */
	 chr *start)				/* where the attempt got started */
{
	struct cnfa *cnfa = d->cnfa;
	int			i;
	unsigned	h;
	struct carc *ca;
	struct sset *p;
	int			ispseudocolor;
	int			ispost;
	int			noprogress;
	int			gotstate;
	int			dolacons;
	int			sawlacons;

	/* for convenience, we can be called even if it might not be a miss */
	if (css->outs[co] != NULL)
	{
		FDEBUG(("hit\n"));
		return css->outs[co];
	}
	FDEBUG(("miss\n"));

	/*
	 * Checking for operation cancel in the inner text search loop seems
	 * unduly expensive.  As a compromise, check during cache misses.
	 */
	if (CANCEL_REQUESTED(v->re))
	{
		ERR(REG_CANCEL);
		return NULL;
	}

	/*
	 * What set of states would we end up in after consuming the co character?
	 * We first consider PLAIN arcs that consume the character, and then look
	 * to see what LACON arcs could be traversed after consuming it.
	 */
	for (i = 0; i < d->wordsper; i++)
		d->work[i] = 0;			/* build new stateset bitmap in d->work */
	ispseudocolor = d->cm->cd[co].flags & PSEUDO;
	ispost = 0;
	noprogress = 1;
	gotstate = 0;
	for (i = 0; i < d->nstates; i++)
		if (ISBSET(css->states, i))
			for (ca = cnfa->states[i]; ca->co != COLORLESS; ca++)
				if (ca->co == co ||
					(ca->co == RAINBOW && !ispseudocolor))
				{
					BSET(d->work, ca->to);
					gotstate = 1;
					if (ca->to == cnfa->post)
						ispost = 1;
					if (!(cnfa->stflags[ca->to] & CNFA_NOPROGRESS))
						noprogress = 0;
					FDEBUG(("%d -> %d\n", i, ca->to));
				}
	if (!gotstate)
		return NULL;			/* character cannot reach any new state */
	dolacons = (cnfa->flags & HASLACONS);
	sawlacons = 0;
	/* outer loop handles transitive closure of reachable-by-LACON states */
	while (dolacons)
	{
		dolacons = 0;
		for (i = 0; i < d->nstates; i++)
			if (ISBSET(d->work, i))
				for (ca = cnfa->states[i]; ca->co != COLORLESS; ca++)
				{
					if (ca->co < cnfa->ncolors)
						continue;	/* not a LACON arc */
					if (ISBSET(d->work, ca->to))
						continue;	/* arc would be a no-op anyway */
					sawlacons = 1;	/* this LACON affects our result */
					if (!lacon(v, cnfa, cp, ca->co))
					{
						if (ISERR())
							return NULL;
						continue;	/* LACON arc cannot be traversed */
					}
					if (ISERR())
						return NULL;
					BSET(d->work, ca->to);
					dolacons = 1;
					if (ca->to == cnfa->post)
						ispost = 1;
					if (!(cnfa->stflags[ca->to] & CNFA_NOPROGRESS))
						noprogress = 0;
					FDEBUG(("%d :> %d\n", i, ca->to));
				}
	}
	h = HASH(d->work, d->wordsper);

	/* Is this stateset already in the cache? */
	for (p = d->ssets, i = d->nssused; i > 0; p++, i--)
		if (HIT(h, d->work, p, d->wordsper))
		{
			FDEBUG(("cached c%d\n", (int) (p - d->ssets)));
			break;				/* NOTE BREAK OUT */
		}
	if (i == 0)
	{							/* nope, need a new cache entry */
		p = getvacant(v, d, cp, start);
		if (p == NULL)
			return NULL;
		assert(p != css);
		for (i = 0; i < d->wordsper; i++)
			p->states[i] = d->work[i];
		p->hash = h;
		p->flags = (ispost) ? POSTSTATE : 0;
		if (noprogress)
			p->flags |= NOPROGRESS;
		/* lastseen to be dealt with by caller */
	}

	/*
	 * Link new stateset to old, unless a LACON affected the result, in which
	 * case we don't create the link.  That forces future transitions across
	 * this same arc (same prior stateset and character color) to come through
	 * miss() again, so that we can recheck the LACON(s), which might or might
	 * not pass since context will be different.
	 */
	if (!sawlacons)
	{
		FDEBUG(("c%d[%d]->c%d\n",
				(int) (css - d->ssets), co, (int) (p - d->ssets)));
		css->outs[co] = p;
		css->inchain[co] = p->ins;
		p->ins.ss = css;
		p->ins.co = co;
	}
	return p;
}

/*
 * lacon - lookaround-constraint checker for miss()
 */
static int						/* predicate:  constraint satisfied? */
lacon(struct vars *v,
	  struct cnfa *pcnfa,		/* parent cnfa */
	  chr *cp,
	  color co)					/* "color" of the lookaround constraint */
{
	int			n;
	struct subre *sub;
	struct dfa *d;
	chr		   *end;
	int			satisfied;

	/* Since this is recursive, it could be driven to stack overflow */
	if (STACK_TOO_DEEP(v->re))
	{
		ERR(REG_ETOOBIG);
		return 0;
	}

	n = co - pcnfa->ncolors;
	assert(n > 0 && n < v->g->nlacons && v->g->lacons != NULL);
	FDEBUG(("=== testing lacon %d\n", n));
	sub = &v->g->lacons[n];
	d = getladfa(v, n);
	if (d == NULL)
		return 0;
	if (LATYPE_IS_AHEAD(sub->latype))
	{
		/* used to use longest() here, but shortest() could be much cheaper */
		end = shortest(v, d, cp, cp, v->stop,
					   (chr **) NULL, (int *) NULL);
		satisfied = LATYPE_IS_POS(sub->latype) ? (end != NULL) : (end == NULL);
	}
	else
	{
		/*
		 * To avoid doing O(N^2) work when repeatedly testing a lookbehind
		 * constraint in an N-character string, we use matchuntil() which can
		 * cache the DFA state across calls.  We only need to restart if the
		 * probe point decreases, which is not common.  The NFA we're using is
		 * a search NFA, so it doesn't mind scanning over stuff before the
		 * nominal match.
		 */
		satisfied = matchuntil(v, d, cp, &v->lblastcss[n], &v->lblastcp[n]);
		if (!LATYPE_IS_POS(sub->latype))
			satisfied = !satisfied;
	}
	FDEBUG(("=== lacon %d satisfied %d\n", n, satisfied));
	return satisfied;
}

/*
 * getvacant - get a vacant state set
 *
 * This routine clears out the inarcs and outarcs, but does not otherwise
 * clear the innards of the state set -- that's up to the caller.
 */
static struct sset *
getvacant(struct vars *v,
		  struct dfa *d,
		  chr *cp,
		  chr *start)
{
	int			i;
	struct sset *ss;
	struct sset *p;
	struct arcp ap;
	color		co;

	ss = pickss(v, d, cp, start);
	if (ss == NULL)
		return NULL;
	assert(!(ss->flags & LOCKED));

	/* clear out its inarcs, including self-referential ones */
	ap = ss->ins;
	while ((p = ap.ss) != NULL)
	{
		co = ap.co;
		FDEBUG(("zapping c%d's %ld outarc\n", (int) (p - d->ssets), (long) co));
		p->outs[co] = NULL;
		ap = p->inchain[co];
		p->inchain[co].ss = NULL;	/* paranoia */
	}
	ss->ins.ss = NULL;

	/* take it off the inarc chains of the ssets reached by its outarcs */
	for (i = 0; i < d->ncolors; i++)
	{
		p = ss->outs[i];
		assert(p != ss);		/* not self-referential */
		if (p == NULL)
			continue;			/* NOTE CONTINUE */
		FDEBUG(("del outarc %d from c%d's in chn\n", i, (int) (p - d->ssets)));
		if (p->ins.ss == ss && p->ins.co == i)
			p->ins = ss->inchain[i];
		else
		{
			struct arcp lastap = {NULL, 0};

			assert(p->ins.ss != NULL);
			for (ap = p->ins; ap.ss != NULL &&
				 !(ap.ss == ss && ap.co == i);
				 ap = ap.ss->inchain[ap.co])
				lastap = ap;
			assert(ap.ss != NULL);
			lastap.ss->inchain[lastap.co] = ss->inchain[i];
		}
		ss->outs[i] = NULL;
		ss->inchain[i].ss = NULL;
	}

	/* if ss was a success state, may need to remember location */
	if ((ss->flags & POSTSTATE) && ss->lastseen != d->lastpost &&
		(d->lastpost == NULL || d->lastpost < ss->lastseen))
		d->lastpost = ss->lastseen;

	/* likewise for a no-progress state */
	if ((ss->flags & NOPROGRESS) && ss->lastseen != d->lastnopr &&
		(d->lastnopr == NULL || d->lastnopr < ss->lastseen))
		d->lastnopr = ss->lastseen;

	return ss;
}

/*
 * pickss - pick the next stateset to be used
 */
static struct sset *
pickss(struct vars *v,
	   struct dfa *d,
	   chr *cp,
	   chr *start)
{
	int			i;
	struct sset *ss;
	struct sset *end;
	chr		   *ancient;

	/* shortcut for cases where cache isn't full */
	if (d->nssused < d->nssets)
	{
		i = d->nssused;
		d->nssused++;
		ss = &d->ssets[i];
		FDEBUG(("new c%d\n", i));
		/* set up innards */
		ss->states = &d->statesarea[i * d->wordsper];
		ss->flags = 0;
		ss->ins.ss = NULL;
		ss->ins.co = WHITE;		/* give it some value */
		ss->outs = &d->outsarea[i * d->ncolors];
		ss->inchain = &d->incarea[i * d->ncolors];
		for (i = 0; i < d->ncolors; i++)
		{
			ss->outs[i] = NULL;
			ss->inchain[i].ss = NULL;
		}
		return ss;
	}

	/* look for oldest, or old enough anyway */
	if (cp - start > d->nssets * 2 / 3) /* oldest 33% are expendable */
		ancient = cp - d->nssets * 2 / 3;
	else
		ancient = start;
	for (ss = d->search, end = &d->ssets[d->nssets]; ss < end; ss++)
		if ((ss->lastseen == NULL || ss->lastseen < ancient) &&
			!(ss->flags & LOCKED))
		{
			d->search = ss + 1;
			FDEBUG(("replacing c%d\n", (int) (ss - d->ssets)));
			return ss;
		}
	for (ss = d->ssets, end = d->search; ss < end; ss++)
		if ((ss->lastseen == NULL || ss->lastseen < ancient) &&
			!(ss->flags & LOCKED))
		{
			d->search = ss + 1;
			FDEBUG(("replacing c%d\n", (int) (ss - d->ssets)));
			return ss;
		}

	/* nobody's old enough?!? -- something's really wrong */
	FDEBUG(("cannot find victim to replace!\n"));
	ERR(REG_ASSERT);
	return NULL;
}

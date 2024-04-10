/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

/* cron.h - header for vixie's cron
 *
 * $Id: cron.h,v 2.10 1994/01/15 20:43:43 vixie Exp $
 *
 * marco 07nov16 [remove code not needed by pg_cron]
 * marco 04sep16 [integrate into pg_cron]
 * vix 14nov88 [rest of log is in RCS]
 * vix 14jan87 [0 or 7 can be sunday; thanks, mwm@berkeley]
 * vix 30dec86 [written]
 */

/* reorder these #include's at your peril */

#include <sys/types.h>
#include <sys/param.h>

#include <stdio.h>
#include <bitstring.h>
#if SYS_TIME_H
# include <sys/time.h>
#else
# include <time.h>
#endif

	/* these are really immutable, and are
	 *   defined for symbolic convenience only
	 * TRUE, FALSE, and ERR must be distinct
	 * ERR must be < OK.
	 */
#define TRUE		1
#define FALSE		0
	/* system calls return this on success */
#define OK		0
	/*   or this on error */
#define ERR		(-1)

	/* turn this on to get '-x' code */
#ifndef DEBUGGING
#define DEBUGGING	FALSE
#endif

#define READ_PIPE	0	/* which end of a pipe pair do you read? */
#define WRITE_PIPE	1	/*   or write to? */
#define STDIN		0	/* what is stdin's file descriptor? */
#define STDOUT		1	/*   stdout's? */
#define STDERR		2	/*   stderr's? */
#define ERROR_EXIT	1	/* exit() with this will scare the shell */
#define	OK_EXIT		0	/* exit() with this is considered 'normal' */
#define	MAX_FNAME	100	/* max length of internally generated fn */
#define	MAX_COMMAND	1000	/* max length of internally generated cmd */
#define	MAX_TEMPSTR	1000	/* max length of envvar=value\0 strings */
#define	MAX_ENVSTR	MAX_TEMPSTR	/* DO NOT change - buffer overruns otherwise */
#define	MAX_UNAME	20	/* max length of username, should be overkill */
#define	ROOT_UID	0	/* don't change this, it really must be root */
#define	ROOT_USER	"root"	/* ditto */

				/* NOTE: these correspond to DebugFlagNames,
				 *	defined below.
				 */
#define	DEXT		0x0001	/* extend flag for other debug masks */
#define	DSCH		0x0002	/* scheduling debug mask */
#define	DPROC		0x0004	/* process control debug mask */
#define	DPARS		0x0008	/* parsing debug mask */
#define	DLOAD		0x0010	/* database loading debug mask */
#define	DMISC		0x0020	/* misc debug mask */
#define	DTEST		0x0040	/* test mode: don't execute any commands */
#define	DBIT		0x0080	/* bit twiddling shown (long) */

#define	CRON_TAB(u)	"%s/%s", SPOOL_DIR, u
#define	REG		register
#define	PPC_NULL	((char **)NULL)

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

#define Is_Blank(c) ((c) == '\t' || (c) == ' ')

#define	Skip_Blanks(c, f) \
			while (Is_Blank(c)) \
				c = get_char(f);

#define	Skip_Nonblanks(c, f) \
			while (c!='\t' && c!=' ' && c!='\n' && c != EOF && c != '\0') \
				c = get_char(f);

#define	Skip_Line(c, f) \
			do {c = get_char(f);} while (c != '\n' && c != EOF);

#if DEBUGGING
# define Debug(mask, message) \
			if ( (DebugFlags & (mask) )  ) \
				printf message;
#else /* !DEBUGGING */
# define Debug(mask, message) \
			;
#endif /* DEBUGGING */

#define	MkLower(ch)	(isupper(ch) ? tolower(ch) : ch)
#define	MkUpper(ch)	(islower(ch) ? toupper(ch) : ch)
#define	Set_LineNum(ln)	{Debug(DPARS|DEXT,("linenum=%d\n",ln)); \
			 LineNumber = ln; \
			}

typedef int time_min;

/* Log levels */
#define	CRON_LOG_JOBSTART	0x01
#define	CRON_LOG_JOBEND		0x02
#define	CRON_LOG_JOBFAILED	0x04
#define	CRON_LOG_JOBPID		0x08

#define	FIRST_MINUTE	0
#define	LAST_MINUTE	59
#define	MINUTE_COUNT	(LAST_MINUTE - FIRST_MINUTE + 1)

#define	FIRST_HOUR	0
#define	LAST_HOUR	23
#define	HOUR_COUNT	(LAST_HOUR - FIRST_HOUR + 1)

#define	FIRST_DOM	1
#define	LAST_DOM	31
#define	DOM_COUNT	(LAST_DOM - FIRST_DOM + 1)

#define	FIRST_MONTH	1
#define	LAST_MONTH	12
#define	MONTH_COUNT	(LAST_MONTH - FIRST_MONTH + 1)

/* note on DOW: 0 and 7 are both Sunday, for compatibility reasons. */
#define	FIRST_DOW	0
#define	LAST_DOW	7
#define	DOW_COUNT	(LAST_DOW - FIRST_DOW + 1)

			/* each user's crontab will be held as a list of
			 * the following structure.
			 *
			 * These are the cron commands.
			 */

typedef	struct _entry {
	struct _entry	*next;
	uid_t		uid;	
	gid_t		gid;
	char		**envp;
	int         secondsInterval;
	bitstr_t	bit_decl(minute, MINUTE_COUNT);
	bitstr_t	bit_decl(hour,   HOUR_COUNT);
	bitstr_t	bit_decl(dom,    DOM_COUNT);
	bitstr_t	bit_decl(month,  MONTH_COUNT);
	bitstr_t	bit_decl(dow,    DOW_COUNT);
	int		flags;
#define	DOM_STAR	0x01
#define	DOW_STAR	0x02
#define	WHEN_REBOOT	0x04
#define MIN_STAR	0x08
#define HR_STAR		0x10
#define DOM_LAST	0x20
} entry;

			/* the crontab database will be a list of the
			 * following structure, one element per user
			 * plus one for the system.
			 *
			 * These are the crontabs.
			 */

typedef	struct _user {
	struct _user	*next, *prev;	/* links */
	char		*name;
	time_t		mtime;		/* last modtime of crontab */
	entry		*crontab;	/* this person's crontab */
#ifdef WITH_SELINUX
        security_context_t scontext;    /* SELinux security context */
#endif
} user;

typedef	struct _cron_db {
	user		*head, *tail;	/* links */
	time_t		user_mtime;     /* last modtime on spooldir */
	time_t		sys_mtime;      /* last modtime on system crontab */
#ifdef DEBIAN
	time_t		sysd_mtime;     /* last modtime on system crondir */
#endif
} cron_db;

typedef struct _orphan {
	struct _orphan  *next;          /* link */
	char            *uname;
	char            *fname;
	char            *tabname;
} orphan;

/*
 * Buffer used to mimick getc(FILE*) and ungetc(FILE*)
 */
#define MAX_FILE_BUFFER_LENGTH 1000

typedef struct _file_buffer {
	char 		data[MAX_FILE_BUFFER_LENGTH];
	int			length;
	int			pointer;
	char		unget_data[MAX_FILE_BUFFER_LENGTH];
	int			unget_count;
} file_buffer;

void	unget_char(int, FILE *),
		free_entry(entry *),
		skip_comments(FILE *);

int		get_char(FILE *),
		get_string(char *, int, FILE *, char *);

entry * parse_cron_entry(char *);

				/* in the C tradition, we only create
				 * variables for the main program, just
				 * extern them elsewhere.
				 */

#ifdef MAIN_PROGRAM
# if !defined(LINT) && !defined(lint)
char	*copyright[] = {
		"@(#) Copyright 1988,1989,1990,1993,1994 by Paul Vixie",
		"@(#) All rights reserved"
	};
# endif

char	*MonthNames[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
		NULL
	};

char	*DowNames[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
		NULL
	};

char	*ecodes[] = {
		"no error",
		"bad minute",
		"bad hour",
		"bad day-of-month",
		"bad month",
		"bad day-of-week",
		"bad command",
		"bad time specifier",
		"bad username",
		"command too long",
		NULL
	};


char	*ProgramName;
int	LineNumber;
time_t	StartTime;
time_min virtualTime;
time_min clockTime;

# if DEBUGGING
int	DebugFlags;
char	*DebugFlagNames[] = {	/* sync with #defines */
		"ext", "sch", "proc", "pars", "load", "misc", "test", "bit",
		NULL		/* NULL must be last element */
	};
# endif /* DEBUGGING */
#else /*MAIN_PROGRAM*/
extern	char	*copyright[],
		*MonthNames[],
		*DowNames[],
		*ProgramName;
extern	int	LineNumber;
extern	time_t	StartTime;
extern  time_min virtualTime;
extern  time_min clockTime;
# if DEBUGGING
extern	int	DebugFlags;
extern	char	*DebugFlagNames[];
# endif /* DEBUGGING */
#endif /*MAIN_PROGRAM*/

/* Copyright 1993,1994 by Paul Vixie
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

/*
 * $Id: pathnames.h,v 1.3 1994/01/15 20:43:43 vixie Exp $
 */

#ifndef CRONDIR
			/* CRONDIR is where crond(8) and crontab(1) both chdir
			 * to; SPOOL_DIR, ALLOW_FILE, DENY_FILE, and LOG_FILE
			 * are all relative to this directory.
			 */
#define CRONDIR		"/var/spool/cron"
#endif

			/* SPOOLDIR is where the crontabs live.
			 * This directory will have its modtime updated
			 * whenever crontab(1) changes a crontab; this is
			 * the signal for crond(8) to look at each individual
			 * crontab file and reload those whose modtimes are
			 * newer than they were last time around (or which
			 * didn't exist last time around...)
			 */
#define SPOOL_DIR	"crontabs"

			/* undefining these turns off their features.  note
			 * that ALLOW_FILE and DENY_FILE must both be defined
			 * in order to enable the allow/deny code.  If neither
			 * LOG_FILE or SYSLOG is defined, we don't log.  If
			 * both are defined, we log both ways.
			 */
#ifdef DEBIAN
#define	ALLOW_FILE	"/etc/cron.allow"		/*-*/
#define DENY_FILE	"/etc/cron.deny"		/*-*/
#else
#define	ALLOW_FILE	"allow"		/*-*/
#define DENY_FILE	"deny"		/*-*/
#endif
/* #define LOG_FILE	"log"		  -*/

			/* where should the daemon stick its PID?
			 */
#ifdef _PATH_VARRUN
# define PIDDIR	_PATH_VARRUN
#else
# define PIDDIR "/etc/"
#endif
#define PIDFILE		"%scrond.pid"

			/* 4.3BSD-style crontab */
#define SYSCRONTAB	"/etc/crontab"
#ifdef DEBIAN
                        /* where package specific crontabs live */ 
#define SYSCRONDIR      "/etc/cron.d"
#endif
			/* what editor to use if no EDITOR or VISUAL
			 * environment variable specified.
			 */
#if defined(DEBIAN)
# define EDITOR "/usr/bin/sensible-editor"
#elif defined(_PATH_VI)
# define EDITOR _PATH_VI
#else
# define EDITOR "/usr/ucb/vi"
#endif

#ifndef _PATH_BSHELL
# define _PATH_BSHELL "/bin/sh"
#endif

#ifndef _PATH_DEFPATH
# define _PATH_DEFPATH "/usr/bin:/bin"
#endif

#ifndef _PATH_DEFPATH_ROOT
# define _PATH_DEFPATH_ROOT "/usr/sbin:/usr/bin:/sbin:/bin"
#endif


#ifndef CRONDIR_MODE
			/* Create mode for CRONDIR; must be in sync with
			 * packaging
			 */
#define CRONDIR_MODE 0755
#endif
#ifndef SPOOL_DIR_MODE
			/* Create mode for SPOOL_DIR; must be in sync with
			 * packaging
			 */
#define SPOOL_DIR_MODE 01730
#endif
#ifndef SPOOL_DIR_GROUP
			/* Chown SPOOL_DIR to this group (needed by Debian's
			 * SGID crontab feature)
			 */ 
#define SPOOL_DIR_GROUP "crontab"
#endif

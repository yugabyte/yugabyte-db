#!/bin/sh

uname=`command -v uname`
sed=`command -v sed`
tr=`command -v tr`
myuname=''
newmyuname=''
trnl=''

case "$test" in
test)
	echo "Hopefully test is built into your sh."
	;;
*)
	if `sh -c "PATH= test true" >/dev/null 2>&1`; then
#		echo "Using the test built into your sh."
		test=test
		_test=test
	fi
	;;
esac

: Find the appropriate value for a newline for tr
if test -n "$DJGPP"; then
       trnl='\012'
fi
if test X"$trnl" = X; then
	case "`echo foo|tr '\n' x 2>/dev/null`" in
	foox) trnl='\n' ;;
	esac
fi
if test X"$trnl" = X; then
	case "`echo foo|tr '\012' x 2>/dev/null`" in
	foox) trnl='\012' ;;
	esac
fi
if test X"$trnl" = X; then
       case "`echo foo|tr '\r\n' xy 2>/dev/null`" in
       fooxy) trnl='\n\r' ;;
       esac
fi
if test X"$trnl" = X; then
	cat <<EOM >&2

$me: Fatal Error: cannot figure out how to translate newlines with 'tr'.

EOM
	exit 1
fi

myuname=`$uname -a 2>/dev/null`
$test -z "$myuname" && myuname=`hostname 2>/dev/null`
# tr '[A-Z]' '[a-z]' would not work in EBCDIC
# because the A-Z/a-z are not consecutive.
myuname=`echo $myuname | $sed -e 's/^[^=]*=//' -e "s,['/],,g" | \
	$tr '[A-Z]' '[a-z]' | $tr $trnl ' '`
newmyuname="$myuname"

: Half the following guesses are probably wrong... If you have better
: tests or hints, please send them to perlbug@perl.org
: The metaconfig authors would also appreciate a copy...
$test -f /irix && osname=irix
$test -f /xenix && osname=sco_xenix
$test -f /dynix && osname=dynix
$test -f /dnix && osname=dnix
$test -f /lynx.os && osname=lynxos
$test -f /unicos && osname=unicos && osvers=`$uname -r`
$test -f /unicosmk && osname=unicosmk && osvers=`$uname -r`
$test -f /unicosmk.ar && osname=unicosmk && osvers=`$uname -r`
$test -f /bin/mips && /bin/mips && osname=mips
$test -d /NextApps && set X `hostinfo | grep 'NeXT Mach.*:' | \
	$sed -e 's/://' -e 's/\./_/'` && osname=next && osvers=$4
$test -d /usr/apollo/bin && osname=apollo
$test -f /etc/saf/_sactab && osname=svr4
$test -d /usr/include/minix && osname=minix
$test -f /system/gnu_library/bin/ar.pm && osname=vos
if $test -d /MachTen -o -d /MachTen_Folder; then
	osname=machten
	if $test -x /sbin/version; then
		osvers=`/sbin/version | $awk '{print $2}' |
		$sed -e 's/[A-Za-z]$//'`
	elif $test -x /usr/etc/version; then
		osvers=`/usr/etc/version | $awk '{print $2}' |
		$sed -e 's/[A-Za-z]$//'`
	else
		osvers="$2.$3"
	fi
fi

$test -f /sys/posix.dll &&
	$test -f /usr/bin/what &&
	set X `/usr/bin/what /sys/posix.dll` &&
	$test "$3" = UWIN &&
	osname=uwin &&
	osvers="$5"

if $test -f $uname; then
	set X $myuname
	shift

	case "$5" in
	fps*) osname=fps ;;
	mips*)
		case "$4" in
		umips) osname=umips ;;
		*) osname=mips ;;
		esac;;
	[23]100) osname=mips ;;
	next*) osname=next ;;
	i386*)
		tmp=`/bin/uname -X 2>/dev/null|awk '/3\.2v[45]/{ print $(NF) }'`
		if $test "$tmp" != "" -a "$3" = "3.2" -a -f '/etc/systemid'; then
			osname='sco'
			osvers=$tmp
		elif $test -f /etc/kconfig; then
			osname=isc
			if test "$lns" = "$ln -s"; then
				osvers=4
			elif $contains _SYSV3 /usr/include/stdio.h > /dev/null 2>&1 ; then
				osvers=3
			elif $contains _POSIX_SOURCE /usr/include/stdio.h > /dev/null 2>&1 ; then
				osvers=2
			fi
		fi
		tmp=''
		;;
	pc*)
		if test -n "$DJGPP"; then
			osname=dos
			osvers=djgpp
		fi
		;;
	esac

	case "$1" in
	aix) osname=aix
		tmp=`( (oslevel) 2>/dev/null || echo "not found") 2>&1`
		case "$tmp" in
		# oslevel can fail with:
		# oslevel: Unable to acquire lock.
		*not\ found) osvers="$4"."$3" ;;
		'<3240'|'<>3240') osvers=3.2.0 ;;
		'=3240'|'>3240'|'<3250'|'<>3250') osvers=3.2.4 ;;
		'=3250'|'>3250') osvers=3.2.5 ;;
		*) osvers=$tmp;;
		esac
		;;
	bsd386) osname=bsd386
		osvers=`$uname -r`
		;;
	cygwin*) osname=cygwin
		osvers="$3"
		;;
	*dc.osx) osname=dcosx
		osvers="$3"
		;;
	dnix) osname=dnix
		osvers="$3"
		;;
	domainos) osname=apollo
		osvers="$3"
		;;
	dgux)	osname=dgux
		osvers="$3"
		;;
	dragonfly) osname=dragonfly
		osvers="$3"
		;;
	dynixptx*) osname=dynixptx
		osvers=`echo "$4"|sed 's/^v//'`
		;;
	freebsd) osname=freebsd
		osvers="$3" ;;
	genix)	osname=genix ;;
	gnu)	osname=gnu
		osvers="$3" ;;
	hp*)	osname=hpux
		osvers=`echo "$3" | $sed 's,.*\.\([0-9]*\.[0-9]*\),\1,'`
		;;
	irix*)	osname=irix
		case "$3" in
		4*) osvers=4 ;;
		5*) osvers=5 ;;
		*)	osvers="$3" ;;
		esac
		;;
	linux)	osname=linux
		case "$3" in
		*)	osvers="$3" ;;
		esac
		;;
	MiNT)	osname=mint
		;;
	netbsd*) osname=netbsd
		osvers="$3"
		;;
	news-os) osvers="$3"
		case "$3" in
		4*) osname=newsos4 ;;
		*) osname=newsos ;;
		esac
		;;
	next*) osname=next ;;
	nonstop-ux) osname=nonstopux ;;
	openbsd) osname=openbsd
            	osvers="$3"
            	;;
	os2)	osname=os2
		osvers="$4"
		;;
	POSIX-BC | posix-bc ) osname=posix-bc
		osvers="$3"
		;;
	powerux | power_ux | powermax_os | powermaxos | \
	powerunix | power_unix) osname=powerux
		osvers="$3"
		;;
	qnx) osname=qnx
		osvers="$4"
		;;
	solaris) osname=solaris
		case "$3" in
		5*) osvers=`echo $3 | $sed 's/^5/2/g'` ;;
		*)	osvers="$3" ;;
		esac
		;;
	sunos) osname=sunos
		case "$3" in
		5*) osname=solaris
			osvers=`echo $3 | $sed 's/^5/2/g'` ;;
		*)	osvers="$3" ;;
		esac
		;;
	titanos) osname=titanos
		case "$3" in
		1*) osvers=1 ;;
		2*) osvers=2 ;;
		3*) osvers=3 ;;
		4*) osvers=4 ;;
		*)	osvers="$3" ;;
		esac
		;;
	ultrix) osname=ultrix
		osvers="$3"
		;;
	osf1|mls+)	case "$5" in
			alpha)
				osname=dec_osf
				osvers=`sizer -v | awk -FUNIX '{print $2}' | awk '{print $1}' |  tr '[A-Z]' '[a-z]' | sed 's/^[xvt]//'`
				case "$osvers" in
				[1-9].[0-9]*) ;;
				*) osvers=`echo "$3" | sed 's/^[xvt]//'` ;;
				esac
				;;
		hp*)	osname=hp_osf1	;;
		mips)	osname=mips_osf1 ;;
		esac
		;;
	# UnixWare 7.1.2 is known as Open UNIX 8
	openunix|unixware) osname=svr5
		osvers="$4"
		;;
	uts)	osname=uts
		osvers="$3"
		;;
	vos) osvers="$3"
		;;
	$2) case "$osname" in
		*isc*) ;;
		*freebsd*) ;;
		svr*)
			: svr4.x or possibly later
			case "svr$3" in
			${osname}*)
				osname=svr$3
				osvers=$4
				;;
			esac
			case "$osname" in
			svr4.0)
				: Check for ESIX
				if test -f /stand/boot ; then
					eval `grep '^INITPROG=[a-z/0-9]*$' /stand/boot`
					if test -n "$INITPROG" -a -f "$INITPROG"; then
		isesix=`strings -a $INITPROG|grep 'ESIX SYSTEM V/386 Release 4.0'`
						if test -n "$isesix"; then
							osname=esix4
						fi
					fi
				fi
				;;
			esac
			;;
		*)	if test -f /etc/systemid; then
				osname=sco
				set `echo $3 | $sed 's/\./ /g'` $4
				if $test -f $src/hints/sco_$1_$2_$3.sh; then
					osvers=$1.$2.$3
				elif $test -f $src/hints/sco_$1_$2.sh; then
					osvers=$1.$2
				elif $test -f $src/hints/sco_$1.sh; then
					osvers=$1
				fi
			else
				case "$osname" in
				'') : Still unknown.  Probably a generic Sys V.
					osname="sysv"
					osvers="$3"
					;;
				esac
			fi
			;;
		esac
		;;
	*)	case "$osname" in
		'') : Still unknown.  Probably a generic BSD.
			osname="$1"
			osvers="$3"
			;;
		esac
		;;
	esac
else
	if test -f /vmunix -a -f $src/hints/news_os.sh; then
		(what /vmunix | UU/tr '[A-Z]' '[a-z]') > UU/kernel.what 2>&1
		if $contains news-os UU/kernel.what >/dev/null 2>&1; then
			osname=news_os
		fi
		$rm -f UU/kernel.what
	elif test -d c:/. -o -n "$is_os2" ; then
		set X $myuname
		osname=os2
		osvers="$5"
	fi
fi

    case "$targetarch" in
    '') ;;
    *)  hostarch=$osname
        osname=`echo $targetarch|sed 's,^[^-]*-,,'`
        osvers=''
        ;;
    esac

: Now look for a hint file osname_osvers, unless one has been
: specified already.
case "$hintfile" in
''|' ')
	file=`echo "${osname}_${osvers}" | $sed -e 's%\.%_%g' -e 's%_$%%'`
	: Also try without trailing minor version numbers.
	xfile=`echo $file | $sed -e 's%_[^_]*$%%'`
	xxfile=`echo $xfile | $sed -e 's%_[^_]*$%%'`
	xxxfile=`echo $xxfile | $sed -e 's%_[^_]*$%%'`
	xxxxfile=`echo $xxxfile | $sed -e 's%_[^_]*$%%'`
	case "$file" in
	'') dflt=none ;;
	*)  case "$osvers" in
		'') dflt=$file
			;;
		*)  if $test -f $src/hints/$file.sh ; then
				dflt=$file
			elif $test -f $src/hints/$xfile.sh ; then
				dflt=$xfile
			elif $test -f $src/hints/$xxfile.sh ; then
				dflt=$xxfile
			elif $test -f $src/hints/$xxxfile.sh ; then
				dflt=$xxxfile
			elif $test -f $src/hints/$xxxxfile.sh ; then
				dflt=$xxxxfile
			elif $test -f "$src/hints/${osname}.sh" ; then
				dflt="${osname}"
			else
				dflt=none
			fi
			;;
		esac
		;;
	esac
	if $test -f Policy.sh ; then
		case "$dflt" in
		*Policy*) ;;
		none) dflt="Policy" ;;
		*) dflt="Policy $dflt" ;;
		esac
	fi
	;;
*)
	dflt=`echo $hintfile | $sed 's/\.sh$//'`
	;;
esac

echo $osname

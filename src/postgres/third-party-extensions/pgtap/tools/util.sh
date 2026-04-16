# Adapted from https://github.com/decibel/db_tools/blob/0.1.10/lib/util.sh

ME=`basename $0`

DEBUG=${DEBUG:-0}

stderr() {
  echo "$@" 1>&2
}

debug() {
  local level=$1
  shift
  if [ $level -le $DEBUG ] ; then
    local oldIFS
    oldIFS=$IFS
    unset IFS
    # Output debug level if it's over threashold
    if [ $DEBUG -ge ${DEBUGEXTRA:-10} ]; then
      stderr "${level}: $@"
    else
      stderr "$@"
    fi
    IFS=$oldIFS
  fi
}

debug_vars () {
  level=$1
  shift
  local out=''
  local value=''
  for variable in $*; do
    eval value=\$$variable
    out="$out $variable='$value'"
  done
  debug $level $out
}

debug_do() {
    local level
    level=$1
    shift
    [ $level -gt $DEBUG ] || ( "$@" )
}

debug_ls() {
    # Reverse test since we *exit* if we shouldn't debug! Also, note that unlike
    # `exit`, `return` does not default to 0.
    [ $1 -le $DEBUG ] || return 0
    (
        level=$1
        shift

        # Look through each argument and see if more than one exist. If so, we don't
        # need to print what it is we're listing.
        location=''
        for a in "$@"; do
            if [ -e "$a" ]; then
                if [ -n "$location" ]; then
                    location=''
                    break
                else
                    location=$a
                fi
            fi
        done

        stderr # blank line
        [ -z "$location" ] || stderr "$location"
        ls "$@" >&2
    )
}

error() {
  local stack lineno
	stack=''
	lineno=''
  while [ "$1" = "-s" -o "$1" = "-n" ]; do
    if [ "$1" = "-n" ]; then
      lineno=$2
      shift 2
    fi
    if [ "$1" = "-s" ]; then
      stack=1
      shift
    fi
  done

  stderr "$@"

  if [ -n "$stack" ]; then
    stacktrace 1 # Skip our own frame
  else
    [ -z "$lineno" ] || echo "File \"$0\", line $lineno" 1>&2
  fi
}

die() {
  local return=$1
  debug_vars 99 return
  shift
  error "$@"
  [ $DEBUG -le 0 ] || stacktrace 1
  if [ -n "${DIE_EXTRA:-}" ]; then
    local lineno=''
    error
    error $DIE_EXTRA
  fi
  exit $return
}

db_exists() {
    local exists
    exists=`psql -qtc "SELECT EXISTS( SELECT 1 FROM pg_database WHERE datname = '$dbname' )" postgres $@ | tr -d ' '`
    if [ "$exists" == "t" ]; then
        return 0
    else
        return 1
    fi
}

stacktrace () {
  debug 200 "stacktrace( $@ )"
  local frame=${1:-0}
  local line=''
  local file=''
  debug_vars 200 frame line file

  # NOTE the stderr redirect below!
  (
    echo
    echo Stacktrace:
    while caller $frame; do
      frame=$(( $frame + 1 ))
    done | while read line function file; do
      if [ -z "$function" -o "$function" = main ]; then
        echo "$file: line $line"
      else
        echo "$file: line $line: function $function"
      fi
    done
  ) 1>&2
}

# This is intended to be used by a trap, ie:
# trap err_report ERR
err_report() {
  stderr "errexit on line $(caller)" >&2
}

find_at_path() (
export PATH="$1:$PATH" # Unfortunately need to maintain old PATH to be able to find `which` :(
out=$(command -v $2)
[ -n "$out" ] || die 2 "unable to find $2"
echo $out
)

# vi: noexpandtab ts=2 sw=2

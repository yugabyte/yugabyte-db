#! /bin/sh
cat $1 | \
sed 's/^ *QUERY PLAN *$/--(snip..)/;s/^-\+$/--(snip..)/;s/^\( \+Foreign File: \).*$/\1 (snip..)/'

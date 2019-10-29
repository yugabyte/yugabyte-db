#! /bin/sh
cat $1 | \
sed 's/^-\+$/--(snip..)/;s/^\( \+Foreign File: \).*$/\1 (snip..)/'

#! /bin/sh
cat $1 | \
sed 's/cost=[\.0-9]*/cost=xxx/;s/width=[0-9]*/width=xxx/;s/^ *QUERY PLAN *$/  QUERY PLAN/;s/^--*$/----------------/' |\
grep -v "Planning time:"

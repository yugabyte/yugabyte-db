# Rules to normalize test outputs. Our custom diff tool passes test output
# of tests through the substitution rules in this file before doing the
# actual comparison.
#
# An example of when this is useful is when an error happens on a different
# port number, or a different worker shard, or a different placement, etc.
# because we are running the tests in a different configuration.

# ignore DEBUG1 messages that Postgres generates
/^DEBUG:  rehashing catalog cache id .*$/d

s/^-[+-]{2,}$/---------------------------------------------------------------------/g
s/^\s+/ /g
s/\s+$//g
# Replace the values of the $$NOW time system variable with a constant
s/\"now\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"now\" : NOW_SYS_VARIABLE/g
s/\"sn\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"sn\" : NOW_SYS_VARIABLE/g
s/Vacuum found (.+) for index=[0-9]+/Vacuum found \1 for index=xxx/g
s/coord_combine_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g
s/worker_partial_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g
s/Vacuum\[index=[0-9]+,vacuumCleanup=/Vacuum\[index=xxx,vacuumCleanup=/g

# Rules to normalize test outputs. Our custom diff tool passes test output
# of tests through the substitution rules in this file before doing the
# actual comparison.
#
# An example of when this is useful is when an error happens on a different
# port number, or a different worker shard, or a different placement, etc.
# because we are running the tests in a different configuration.


# Differing names can have differing table column widths
s/^-[+-]{2,}$/---------------------------------------------------------------------/g
# Replace the values of the time system variables ($$NOW) with a constant
s/\"sn\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"sn\" : NOW_SYS_VARIABLE/g
s/\"now\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"now\" : NOW_SYS_VARIABLE/g
s/TTL job elapsed time: [+-]?[0-9]*\.?[0-9]+([eE][+-]?[0-9]+)?ms,/TTL job elapsed time:<redacted>/g
s/expiry_cutoff=[0-9]*,/expiry_cutoff=<redacted>/g
s/coord_combine_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g
s/worker_partial_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g

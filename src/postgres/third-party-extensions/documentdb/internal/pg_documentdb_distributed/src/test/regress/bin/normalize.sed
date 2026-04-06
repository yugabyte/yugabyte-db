# Rules to normalize test outputs. Our custom diff tool passes test output
# of tests through the substitution rules in this file before doing the
# actual comparison.
#
# An example of when this is useful is when an error happens on a different
# port number, or a different worker shard, or a different placement, etc.
# because we are running the tests in a different configuration.


# Differing names can have differing table column widths
s/^-[+-]{2,}$/---------------------------------------------------------------------/g
s/^\s+/ /g
s/\s+$//g
s/Memory Usage: [0-9]+kB/Memory Usage: XXXkB/g
s/Memory: [0-9]+kB/Memory: XXXkB/g
s/process [0-9]+ still waiting for ShareLock on transaction [0-9]+ after [0-9\.]+ ms/process XYZ still waiting on ShareLock on transaction T1 after D1 ms/g
s/process [0-9]+ acquired ShareLock on transaction [0-9]+ after [0-9\.]+ ms/process XYZ acquired ShareLock on transaction T1 after D1 ms/g
s/Distributed Subplan \d+/Distributed Subplan DDD/g
s/Distributed Subplan [0-9]+_[0-9]+/Distributed Subplan X_X/g
s/read_intermediate_result\('[0-9]+_[0-9]+'::text/read_intermediate_result\('X_X'::text/g
s/Type oid not supported \d+/Type oid not supported ddd/g
# Replace the values of the $$NOW time system variable with a constant
s/\"now\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"now\" : NOW_SYS_VARIABLE/g
s/\"sn\" : \{ \"\$date\" : \{ \"\$numberLong\" : \"[0-9]*\" \} \}/\"sn\" : NOW_SYS_VARIABLE/g
s/documentdb_api_catalog.shard_key_and_document/shard_key_and_document/g
s/documentdb_api_internal.generate_unique_shard_document/generate_unique_shard_document/g
s/documentdb_core.bson/bson/g
s/TTL job elapsed time: [+-]?[0-9]*\.?[0-9]+([eE][+-]?[0-9]+)?ms,/TTL job elapsed time:<redacted>/g
s/expiry_cutoff=[0-9]*,/expiry_cutoff=<redacted>/g
s/coord_combine_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g
s/worker_partial_agg\('[0-9]+'/coord_combine_agg\('xxxx'/g
s/Node: host=localhost port=[0-9]+ dbname=[a-zA-Z]+/Node: host=localhost port=xxx dbname=yyy/g

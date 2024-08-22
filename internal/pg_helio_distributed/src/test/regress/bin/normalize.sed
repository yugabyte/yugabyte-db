# Rules to normalize test outputs. Our custom diff tool passes test output
# of tests through the substitution rules in this file before doing the
# actual comparison.
#
# An example of when this is useful is when an error happens on a different
# port number, or a different worker shard, or a different placement, etc.
# because we are running the tests in a different configuration.


# Differing names can have differing table column widths
s/^-[+-]{2,}$/---------------------------------------------------------------------/g
s/process [0-9]+ still waiting for ShareLock on transaction [0-9]+ after [0-9\.]+ ms/process XYZ still waiting on ShareLock on transaction T1 after D1 ms/g
s/process [0-9]+ acquired ShareLock on transaction [0-9]+ after [0-9\.]+ ms/process XYZ acquired ShareLock on transaction T1 after D1 ms/g
s/Distributed Subplan \d+/Distributed Subplan DDD/g
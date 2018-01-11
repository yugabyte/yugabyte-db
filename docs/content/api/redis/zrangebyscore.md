---
title: ZRANGEBYSCORE
weight: 2390
---

## SYNOPSIS
<b>`ZRANGEBYSCORE key min max [WITHSCORES]`</b><br>
This command fetches `members` for which `score` is in the given `min` `max` range. `min` and `max` are doubles. 
If `key` does not exist, an empty range is returned. If `key` corresponds to a non
sorted-set, an error is raised. Special bounds `-inf` and `+inf` are also supported to retrieve an entire range.
`min` and `max` are inclusive unless they are prefixed with `(`, in which case they are
exclusive.


## RETURN VALUE
Returns a list of `members` found in the range specified by `min`, `max`, unless the WITHSCORES option is specified (see below).

## ZRANGEBYSCORE Options
<li> WITHSCORES: Makes the command return both the `member` and its `score`.</li>

## EXAMPLES
```
$ ZADD z_key 1.0 v1 2.0 v2
(integer) 2
# Should retrieve all members.
$ ZRANGEBYSCORE z_key +inf -inf
1) "v1"
2) "v2"
# Should retrive all member score pairs.
$ ZRANGEBYSCORE z_key +inf -inf WITHSCORES
1) "v1"
2) "1.0"
3) "v2"
4) "2.0"
# Bounds are inclusive.
$ ZRANGEBYSCORE z_key 1.0 2.0
1) "v1"
2) "v2"
# Bounds are exclusive.
$ ZRANGEBYSCORE z_key (1.0 (2.0
(empty list or set)
```
## SEE ALSO
[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrem`](../zrem/)

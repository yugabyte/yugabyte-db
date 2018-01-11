---
title: ZREM
weight: 2391
---

## SYNOPSIS
<b>`ZREM key member [member...]`</b><br>
This command removes the `members` specified in the sorted set at `key` and returns the number of `members` removed. 
`Members` specified that do not exist in key are ignored. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## RETURN VALUE

The number of `members` removed from the sorted set.

## EXAMPLES
```
$ ZADD z_key 1.0 v1 2.0 v2
(integer) 2
$ ZREM z_key v2 v3
(integer) 1
$ ZREM z_key v1 v2 v3
(integer) 1
$ ZCARD z_key 
(integer) 0
```
## SEE ALSO
[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrangebyscore`](../zrangebyscore/)

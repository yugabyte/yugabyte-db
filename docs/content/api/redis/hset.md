---
title: HSET
---

## SYNOPSIS
<b>`HSET key field value`</b><br>
This command sets the data for the given `field` of the hash that is associated with the given `key` with the given `value`. If the `field` already exists in the hash, it is overwritten.

<li>If the given `key` does not exist, an associated hash is created, and the `field` and `value` are inserted.</li>
<li>If the given `key` is not associated with a hash, an error is raised.</li>

## RETURN VALUE
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is true, returns
 1 if a new field is inserted and 0 if an existing field is updated.
</li>
<li>
If `emulate_redis_responses` is false, returns
 OK
</li>


## EXAMPLES
<li> `emulate_redis_responses` is `true`.
```
$ HSET yugahash area1 "America"
1
$ HSET yugahash area1 "North America"
0
$ HGET yugahash area1
"North America"
```
</li>

<li> `emulate_redis_responses` is `false`.
```
$ HSET yugahash area1 "America"
OK
$ HSET yugahash area1 "North America"
OK
$ HGET yugahash area1
"North America"
```
</li>

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)

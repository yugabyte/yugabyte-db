---
title: HDEL
---

## SYNOPSIS
<b>`HDEL key field [field ...]`</b><br>
This command removes the given `fields` from the hash that is associated with the given `key`.

<li>If the given `key` does not exist, it is characterized as an empty hash, and 0 is returned for no elements are removed.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is `true`, returns
the number of existing fields in the hash that were removed by this command.
</li>
<li>
If `emulate_redis_responses` is `false`, returns OK.
</li>


## EXAMPLES
<li> `emulate_redis_responses` is `true`.
```
$ HSET yugahash moon "Moon"
1
$ HDEL yugahash moon
1
$ HDEL yugahash moon
0
```
</li>

<li> `emulate_redis_responses` is `false`.
```
$ HSET yugahash moon "Moon"
OK
$ HDEL yugahash moon
OK
$ HDEL yugahash moon
OK
```
</li>

## SEE ALSO
[`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)

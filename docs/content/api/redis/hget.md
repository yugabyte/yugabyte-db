---
title: HGET
---

## SYNOPSIS
<code><b>HGET key field</b></code><br>
This command is to fetch the value for the given <code>field</code> in the hash that is specified by the given <code>key</code>.

<li>If the given <code>key</code> or <code>field</code> does not exist, null is returned.</li>
<li>If the given <code>key</code> is associated a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns the value for the given <code>field</code>

## EXAMPLES
% <code>HSET yugahash area1 "America"</code><br>
1<br>
% <code>HGET yugahash area1</code><br>
"America"<br>

## SEE ALSO
[`hdel`](/api/redis/hdel/), [`hexists`](/api/redis/hexists/), [`hgetall`](/api/redis/hgetall/), [`hkeys`](/api/redis/hkeys/), [`hlen`](/api/redis/hlen/), [`hmget`](/api/redis/hmget/), [`hmset`](/api/redis/hmset/), [`hset`](/api/redis/hset/), [`hstrlen`](/api/redis/hstrlen/), [`hvals`](/api/redis/hvals/)

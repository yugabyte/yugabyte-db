---
title: HMSET
---

## SYNOPSIS
<code><b>HMSET key field value [field value ...]</b></code><br>
This command is to set the data for the given <code>field</code> with the given <code>value</code> in the hash that is specified by <code>key</code>.
<li>If the given <code>field</code> already exists in the specified hash, this command overwrites the existing value with the given <code>value</code>.</li>
<li>If the given <code>key</code> does not exist, a new hash is created for the <code>key</code>, and the given values are inserted to the associated given fields.</li>
<li>If the given <code>key</code> is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
% <code>HMSET yugahash area1 "America" area2 "Africa"</code><br>
"OK"<br>
% <code>HGET yugahash area1</code><br>
"America"<br>

## SEE ALSO
[`hdel`](/api/redis/hdel/), [`hexists`](/api/redis/hexists/), [`hget`](/api/redis/hget/), [`hgetall`](/api/redis/hgetall/), [`hkeys`](/api/redis/hkeys/), [`hlen`](/api/redis/hlen/), [`hmget`](/api/redis/hmget/), [`hset`](/api/redis/hset/), [`hstrlen`](/api/redis/hstrlen/), [`hvals`](/api/redis/hvals/)

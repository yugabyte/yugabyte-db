---
title: HKEYS
---

## SYNOPSIS
<code><b>HKEYS key</b></code><br>
This command is to fetch all fields of the hash that is associated with the given <code>key</code>.

<li>If the <code>key</code> does not exist, an empty list is returned.</li>
<li>If the <code>key</code> is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of fields in the specified hash.

## EXAMPLES
% <code>HSET yugahash area1 "Africa"</code><br>
1<br>
% <code>HSET yugahash area2 "America"</code><br>
1<br>
% <code>HKEYS yugahash</code><br>
1) "area1"<br>
1) "area2"<br>

## SEE ALSO
[`hdel`](/yql/redis/hdel/), [`hexists`](/yql/redis/hexists/), [`hget`](/yql/redis/hget/), [`hgetall`](/yql/redis/hgetall/), [`hlen`](/yql/redis/hlen/), [`hmget`](/yql/redis/hmget/), [`hmset`](/yql/redis/hmset/), [`hset`](/yql/redis/hset/), [`hstrlen`](/yql/redis/hstrlen/), [`hvals`](/yql/redis/hvals/)

---
title: HSTRLEN
---

## SYNOPSIS
% <code><b>HSTRLEN key field</b></code><br>
This command is to seek the length of a string value that is associated with the given <code>field</code> in a hash table that is associated with the given <code>key</code>.
<li>If the <code>key</code> or <code>field</code> does not exist, 0 is returned.</li>
<li>If the <code>key</code> is associated with a non-hash-table value, an error is raised.</li>

## RETURN VALUE
Returns the length of the specified string.

## EXAMPLES
% <code>HMSET yugahash L1 America L2 Europe</code><br>
"OK"<br>
% <code>HSTRLEN yugahash L1</code><br>
7<br>

## SEE ALSO
[`hdel`](/yql/redis/hdel/), [`hexists`](/yql/redis/hexists/), [`hget`](/yql/redis/hget/), [`hgetall`](/yql/redis/hgetall/), [`hkeys`](/yql/redis/hkeys/), [`hlen`](/yql/redis/hlen/), [`hmget`](/yql/redis/hmget/), [`hmset`](/yql/redis/hmset/), [`hset`](/yql/redis/hset/), [`hvals`](/yql/redis/hvals/)

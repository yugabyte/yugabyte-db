---
title: DEL
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## SYNOPSIS
<code><b>DEL key [key ...]</b></code><br>
This command is to delete the given <code>key</code>.

<li>If the <code>key</code> does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## RETURN VALUE
Returns number of keys that were removed.

## EXAMPLES
% <code>SET yuga1 "America"</code><br>
"OK"<br>
% <code>SET yuga2 "Africa"</code><br>
"OK"<br>
% <code>DEL yuga1 yuga2 not_a_key</code><br>
2<br>

## SEE ALSO
[`exists`](/yql/redis/exists/), [`get`](/yql/redis/get/), [`getrange`](/yql/redis/getrange/), [`hdel`](/yql/redis/hdel/), [`hexists`](/yql/redis/hexists/), [`hget`](/yql/redis/hget/), [`hset`](/yql/redis/hset/), [`mget`](/yql/redis/mget/), [`mset`](/yql/redis/mset/), [`sadd`](/yql/redis/sadd/), [`set`](/yql/redis/set/)

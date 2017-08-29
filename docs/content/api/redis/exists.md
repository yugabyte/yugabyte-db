---
title: EXISTS
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## SYNOPSIS
<code><b>EXISTS key [key ...]</b></code><br>
This command is a predicate to check whether or not the given <code>key</code> exists.

## RETURN VALUE
Returns the number of existing keys.

## EXAMPLES
% <code>SET yuga1 "Africa"</code><br>
"OK"<br>
% <code>SET yuga2 "America"</code><br>
"OK"<br>
% <code>EXISTS yuga1</code><br>
1<br>
% <code>EXISTS yuga1 yuga2 not_a_key</code><br>
2<br>

## SEE ALSO
[`del`](/api/redis/del/), [`get`](/api/redis/get/), [`getrange`](/api/redis/getrange/), [`hdel`](/api/redis/hdel/), [`hexists`](/api/redis/hexists/), [`hget`](/api/redis/hget/), [`hset`](/api/redis/hset/), [`mget`](/api/redis/mget/), [`mset`](/api/redis/mset/), [`sadd`](/api/redis/sadd/), [`set`](/api/redis/set/)

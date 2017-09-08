---
title: EXISTS
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## SYNOPSIS
<b>`EXISTS key [key ...]`</b><br>
This command is a predicate to check whether or not the given `key` exists.

## RETURN VALUE
Returns the number of existing keys.

## EXAMPLES
```
$ SET yuga1 "Africa"
"OK"
$ SET yuga2 "America"
"OK"
$ EXISTS yuga1
1
$ EXISTS yuga1 yuga2 not_a_key
2
```

## SEE ALSO
[`del`](../del/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`mget`](../mget/), [`mset`](../mset/), [`sadd`](../sadd/), [`set`](../set/)

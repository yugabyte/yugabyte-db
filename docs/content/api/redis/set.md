---
title: SET
---

## SYNOPSIS
<b>`SET key string_value [EX seconds] [PX milliseconds] [NX|XX]`</b><br>
This command inserts `string_value` to be hold at `key`, where `EX seconds` sets the expire time in `seconds`, `PX milliseconds` sets the expire time in `milliseconds`, `NX` sets the key only if it does not already exist, and `XX` sets the key only if it already exists.

<li>If the `key` is already associated with a value, it is overwritten regardless of its type.</li>
<li>All parameters that are associated with the `key`, such as datatype and time to live, are discarded.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
```
$ SET yugakey "YugaByte"
"OK"
$ GET yugakey
"YugaByte"
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`setrange`](../setrange/), [`strlen`](../strlen/)

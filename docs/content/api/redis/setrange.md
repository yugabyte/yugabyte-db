---
title: SETRANGE
weight: 2280
---
## SYNOPSIS
<b>`SETRANGE key offset value`</b><br>
This command overwrites the string that is associated with the given `key` with the given `value`, starting from the given `offset`.
<li> The `offset` cannot exceed 536870911.</li>
<li>If the `offset` is larger than the length of the specified string, the string will be padded with zeros up to the `offset`.</li>
<li>If the `key` does not exist, its associated string is an empty string. The resulted new string is constructed with zeros up to the given `offset` and then appended with the given `value`.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns the length of the resulted string after overwriting.

## EXAMPLES
```
$ SET yugakey "YugaKey"
"OK"
$ SETRANGE yugakey 4 "Byte"
8
$ GET yugakey
"YugaByte"
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`strlen`](../strlen/)

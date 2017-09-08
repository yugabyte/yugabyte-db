---
title: INCR
---

## SYNOPSIS
<b>`INCR key`</b><br>
This command is to add 1 to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.
<li>If the `key` does not exist, the associated string is set to "0".</li>
<li>If the given `key` is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.</li>

## RETURN VALUE
Returns the value after addition.

## EXAMPLES
```
$ SET yugakey 7
"OK"
$ INCR yugakey
8
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)

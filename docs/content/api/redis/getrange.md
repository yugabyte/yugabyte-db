---
title: GETRANGE
---

## SYNOPSIS
<b>`GETRANGE key start end`</b><br>
This command is to fetch a substring of the string value that is associated with the given `key` between the two given offsets `start` and `end`. If an offset value is positive, it is counted from the beginning of the string. If an offset is negative, it is counted from the end. If an offset is out of range, it is limited to either the beginning or the end of the string.
<li>If `key` does not exist, (null) is returned.</li>
<li>If `key` is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns a string value.

## EXAMPLES
```
$ SET yugakey "YugaByte"
"OK"
$ GETRANGE yugakey 0 3
"Yuga"
$ GETRANGE yugakey -4 -1
"Byte"
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
---
title: STRLEN
---

## SYNOPSIS
<b>`STRLEN key`</b><br>
This command finds the length of the string value that is associated with the given `key`.
<li> If `key` is associated with a non-string value, an error is raised.</li>
<li> If `key` does not exist, 0 is returned.</li>

## RETURN VALUE
Returns length of the specified string.

## EXAMPLES
```
$ SET yugakey "string value"
"OK"
$ STRLEN yugakey
12
$ STRLEN undefined_key
0
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/)

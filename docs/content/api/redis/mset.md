---
title: MSET
weight: 2230
---

## SYNOPSIS
<b>`MSET key value [key value ...]`</b><br>
This command is an atomic write that sets the data for all given `keys` with their associated `values`.

<li>If a `key` already exists, it is overwritten regardless of its datatype.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
```
$ MSET yuga1 "Africa" yuga2 "America"
"OK"
$ GET yuga1
"Africa"
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
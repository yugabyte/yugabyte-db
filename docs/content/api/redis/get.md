---
title: GET
weight: 2070
---

## SYNOPSIS
<b>`GET key`</b><br>
This command fetches the value that is associated with the given `key`.

<li>If the `key` does not exist, null is returned.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns string value of the given `key`.

## EXAMPLES
```
$ GET yugakey
(null)
$ SET yugakey "YugaByte"
"OK"
$ GET yugakey
"YugaByte"
```

## SEE ALSO
[`append`](../append/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)

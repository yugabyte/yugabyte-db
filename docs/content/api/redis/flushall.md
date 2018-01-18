---
title: FLUSHALL
weight: 2064
---

## SYNOPSIS
<b>`FLUSHALL`</b><br>
This command deletes all keys from all databases.

## RETURN VALUE
Returns status string.

## EXAMPLES
```
$ SET yuga1 "America"
"OK"
$ SET yuga2 "Africa"
"OK"
$ GET yuga1
"America"
$ GET yuga2
"Africa"
$ FLUSHALL
"OK"
$ GET yuga1
(null)
$ GET yuga2
(null)
```

## SEE ALSO
[`del`](../del/), [`flushdb`](../flushdb/)

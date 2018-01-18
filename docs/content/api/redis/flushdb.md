---
title: FLUSHDB
weight: 2065
---

## SYNOPSIS
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

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
$ FLUSHDB
"OK"
$ GET yuga1
(null)
$ GET yuga2
(null)
```

## SEE ALSO
[`del`](../del/), [`flushall`](../flushall/)

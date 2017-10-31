---
title: TSREM
weight: 231
---

## SYNOPSIS
<b>`TSREM key timestamp [timestamp ...]`</b><br>
This command removes one or more specified timestamps from the time series that is associated with the given `key`.
<li>If the `key` is associated with a value that is not a set, an error is raised.</li>
<li>If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.</li>
<li>If the provided timestamps don't exist, TSRem still returns "OK". As a result, TSRem just
ensures the provided timestamps no longer exist, but doesn't provide any information about whether
they existed before the command was run.</li>

## RETURN VALUE
Returns the appropriate status string.

## EXAMPLES
```
$ TSAdd cpu_usage 10 “70”
“OK”
$ TSAdd cpu_usage 20 “80” 30 “60” 40 “90”
“OK”
$ TSAdd cpu_usage 201710311100 “50”
“OK”
$ TSAdd cpu_usage 1509474505 “75”
“OK”

$ TSRem cpu_usage 20 30 40
“OK”
$ TSRangeByTime cpu_usage 10 40
1) 10 
2) “70"
$ TSRem cpu_usage 1509474505
“OK”
$ TSGet cpu_usage 1509474505
(nil)
```

## SEE ALSO
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrangebytime`](../tsrangebytime/)

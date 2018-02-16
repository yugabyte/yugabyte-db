---
title: TSGET
weight: 2340
---

## SYNOPSIS
<b>`TSGET key timestamp`</b><br>
This command fetches the value for the given `timestamp` in the time series that is specified by the 
given `key`.

<li>If the given `key` or `timestamp` does not exist, nil is returned.</li>
<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.</li>

## RETURN VALUE
Returns the value for the given `timestamp`

## EXAMPLES
```
# The timestamp can be arbitrary integers used just for sorting values in a certain order.
$ TSAdd cpu_usage 10 “70”
“OK”
$ TSAdd cpu_usage 20 “80” 30 “60” 40 “90”
“OK”
# We could also encode the timestamp as “yyyymmddhhmm”, since this would still 
# produce integers that are sortable by the actual timestamp.
$ TSAdd cpu_usage 201710311100 “50”
“OK”
# A more common option would be to specify the timestamp as the unix timestamp
$ TSAdd cpu_usage 1509474505 “75”
“OK”

$ TSGet cpu_usage 10
“70”
$ TSGet cpu_usage 100
(nil)
$ TSGet cpu_usage 201710311100
“50”
$ TSGet cpu_usage 1509474505
“75”
$ TSGet cpu_usage xyz # timestamp is not int64.
(error) Request was unable to be processed from server.
```

## SEE ALSO
[`tsadd`](../tsadd/), [`tsrem`](../tsrem/), [`tsrangebytime`](../tsrangebytime/)

---
title: TSRANGEBYTIME
weight: 212
---

## SYNOPSIS
<b>`TSRANGEBYTIME key low_ts high_ts`</b><br>
This command fetches the values for the given `low_ts`, `high_ts` range in the time series that is 
specified by the given `key`.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `low_ts` or `high_ts` are not a valid signed 64 bit integers, an error is raised.</li>
<li>`low_ts` and `high_ts` are inclusive unless they are prefixed with `(`, in that case they are
exclusive.</li>
<li>Special bounds `-inf` and `+inf` are also supported to retrieve an entire range</li>
<li>Its more efficient to query the higher timestamp values (usually the common use-case) 
    since they denote the latest values.</li>

## RETURN VALUE
Returns a list of timestamp, value pairs found in the range specified by `low_ts`, `high_ts`

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

$ TSRangeByTime cpu_usage 20 40
1) 20
2) “80” 
3) 30
4) “60” 
5) 40
6) “90”
$ TSRangeByTime cpu_usage (20 40 # 20 is exclusive
1) 30
2) “60”
3) 40
4) “90”
$ TSRangeByTime cpu_usage (20 (40 # 20 and 40 are exclusive
1) 30
2) “60”
$ TSRangeByTime cpu_usage -inf 30
1) 10
2) “70”
3) 20
4) “80”
5) 30
6) “60”
$ TSRangeByTime cpu_usage 20 +inf
1) 20
2) “80”
3) 30
4) “60”
5) 40
6) “90”
```

## SEE ALSO
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrem`](../tsrem/)

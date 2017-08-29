---
title: MGET
---

## SYNOPSIS
<code><b>MGET key [key ...]</b></code><br>
This command is to collect string values of all given keys.
<li>If a given <code>key</code> does not exist, an empty string is returned for that <code>key</code>.</li>
<li>If a given <code>key</code> is associated with a non-string value, an empty string is returned for that <code>key</code>.</li>

## RETURN VALUE
Returns an array of string values.

## EXAMPLES
% <code>MGET yuga_area1 yuga_area2 yuga_none</code><br>
1) "Africa"<br>
2) "America"<br>
3) (null)<br>

## SEE ALSO
[`append`](/yql/redis/append/), [`get`](/yql/redis/get/), [`getrange`](/yql/redis/getrange/), [`getset`](/yql/redis/getset/), [`incr`](/yql/redis/incr/), [`mset`](/yql/redis/mset/), [`set`](/yql/redis/set/), [`setrange`](/yql/redis/setrange/), [`strlen`](/yql/redis/strlen/)
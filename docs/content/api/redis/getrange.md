---
title: GETRANGE
---

## SYNOPSIS
<code><b>GETRANGE key start end</b></code><br>
This command is to fetch a substring of the string value that is associated with the given <code>key</code> between the two given offsets <code>start</code> and <code>end</code>. If an offset value is positive, it is counted from the beginning of the string. If an offset is negative, it is counted from the end. If an offset is out of range, it is limited to either the beginning or the end of the string.
<li>If <code>key</code> does not exist, (null) is returned.</li>
<li>If <code>key</code> is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns a string value.

## EXAMPLES
% <code>SET yugakey "YugaByte"</code><br>
"OK"<br>
% <code>GETRANGE yugakey 0 3</code><br>
"Yuga"<br>
% <code>GETRANGE yugakey -4 -1</code><br>
"Byte"<br>

## SEE ALSO
[`append`](/api/redis/append/), [`get`](/api/redis/get/), [`getset`](/api/redis/getset/), [`incr`](/api/redis/incr/), [`mget`](/api/redis/mget/), [`mset`](/api/redis/mset/), [`set`](/api/redis/set/), [`setrange`](/api/redis/setrange/), [`strlen`](/api/redis/strlen/)
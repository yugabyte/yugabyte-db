---
title: STRLEN
---

## SYNOPSIS
<code><b>STRLEN key</b></code><br>
This command is to find the length of the string value that is associated with the given <code>key</code>.
<li> If <code>key</code> is associated with a non-string value, an error is raised.</li>
<li> If <code>key</code> does not exist, 0 is returned.</li>

## RETURN VALUE
Returns length of the specified string.

## EXAMPLES
% <code>SET yugakey "string value"</code><br>
"OK"<br>
% <code>STRLEN yugakey</code><br>
12<br>
% <code>STRLEN undefined_key</code><br>
0<br>

## SEE ALSO
[`append`](/yql/redis/append/), [`get`](/yql/redis/get/), [`getrange`](/yql/redis/getrange/), [`getset`](/yql/redis/getset/), [`incr`](/yql/redis/incr/), [`mget`](/yql/redis/mget/), [`mset`](/yql/redis/mset/), [`set`](/yql/redis/set/), [`setrange`](/yql/redis/setrange/)

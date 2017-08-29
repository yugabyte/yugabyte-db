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
[`append`](/api/redis/append/), [`get`](/api/redis/get/), [`getrange`](/api/redis/getrange/), [`getset`](/api/redis/getset/), [`incr`](/api/redis/incr/), [`mget`](/api/redis/mget/), [`mset`](/api/redis/mset/), [`set`](/api/redis/set/), [`setrange`](/api/redis/setrange/)

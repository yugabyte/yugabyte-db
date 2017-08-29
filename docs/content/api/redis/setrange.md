---
title: SETRANGE
---
## SYNOPSIS
<code><b>SETRANGE key offset value</b></code><br>
This commands is to overwrite the string that is associated with the given <code>key</code> with the given <code>value</code>, starting from the given <code>offset</code>.
<li> The <code>offset</code> cannot exceed 536870911.</li>
<li>If the <code>offset</code> is larger than the length of the specified string, the string will be padded with zeros up to the <code>offset</code>.</li>
<li>If the <code>key</code> does not exist, its associated string is an empty string. The resulted new string is constructed with zeros up to the given <code>offset</code> and then appended with the given <code>value</code>.</li>
<li>If the <code>key</code> is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns the length of the resulted string after overwriting.

## EXAMPLES
% <code>SET yugakey "YugaKey"</code><br>
"OK"<br>
% <code>SETRANGE yugakey 4 "Byte"</code><br>
8<br>
% <code>GET yugakey</code><br>
"YugaByte"<br>

## SEE ALSO
[`append`](/api/redis/append/), [`get`](/api/redis/get/), [`getrange`](/api/redis/getrange/), [`getset`](/api/redis/getset/), [`incr`](/api/redis/incr/), [`mget`](/api/redis/mget/), [`mset`](/api/redis/mset/), [`set`](/api/redis/set/), [`strlen`](/api/redis/strlen/)

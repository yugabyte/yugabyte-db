---
title: GET
---

## SYNOPSIS
<code><b>GET key</code></b><br>
This command is to fetch the value that is associated with the given <code>key</code>.

<li>If the <code>key</code> does not exist, null is returned.</li>
<li>If the <code>key</code> is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns string value of the given <code>key</code>.

## EXAMPLES
% <code>GET yugakey</code><br>
(null)<br>
% <code>SET yugakey "YugaByte"</code><br>
"OK"<br>
% <code>GET yugakey</code><br>
"YugaByte"<br>

## SEE ALSO
[`append`](/api/redis/append/), [`getrange`](/api/redis/getrange/), [`getset`](/api/redis/getset/), [`incr`](/api/redis/incr/), [`mget`](/api/redis/mget/), [`mset`](/api/redis/mset/), [`set`](/api/redis/set/), [`setrange`](/api/redis/setrange/), [`strlen`](/api/redis/strlen/)
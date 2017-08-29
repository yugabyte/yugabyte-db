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
[`append`](/yql/redis/append/), [`getrange`](/yql/redis/getrange/), [`getset`](/yql/redis/getset/), [`incr`](/yql/redis/incr/), [`mget`](/yql/redis/mget/), [`mset`](/yql/redis/mset/), [`set`](/yql/redis/set/), [`setrange`](/yql/redis/setrange/), [`strlen`](/yql/redis/strlen/)
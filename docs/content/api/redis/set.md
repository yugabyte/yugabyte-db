---
title: SET
---

## SYNOPSIS
<code><b>SET key string_value [EX seconds] [PX milliseconds] [NX|XX]</b></code><br>
This command is to insert <code>string_value</code> to be hold at <code>key</code>, where <code>EX seconds</code> sets the expire time in <code>seconds</code>, <code>PX milliseconds</code> sets the expire time in <code>milliseconds</code>, <code>NX</code> sets the key only if it does not already exist, and <code>XX</code> sets the key only if it already exists.

<li>If the <code>key</code> is already associated with a value, it is overwritten regardless of its type.</li>
<li>All parameters that are associated with the <code>key</code>, such as datatype and time to live, are discarded.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
% <code>SET yugakey "YugaByte"</code><br>
% "OK"<br>
% <code>GET yugakey</code><br>
% "YugaByte"<br>

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`setrange`](../setrange/), [`strlen`](../strlen/)
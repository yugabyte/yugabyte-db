---
title: MSET
---

## SYNOPSIS
<code><b>MSET key value [key value ...]</b></code><br>
This command is an atomic write that sets the data for all given <code>keys</code> with their associated <code>values</code>.

<li>If a <code>key</code> already exists, it is overwritten regardless of its datatype.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
% <code>MSET yuga1 "Africa" yuga2 "America"</code><br>
"OK"<br>
% <code>GET yuga1</code><br>
"Africa"<br>

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
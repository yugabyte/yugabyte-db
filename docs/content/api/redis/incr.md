---
title: INCR
---

## SYNOPSIS
<code><b>INCR key</b></code><br>
This command is to add 1 to the number that is associated with the given <code>key</code>. The numeric value must a 64-bit signed integer.
<li>If the <code>key</code> does not exist, the associated string is set to "0".</li>
<li>If the given <code>key</code> is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.</li>

## RETURN VALUE
Returns the value after addition.

## EXAMPLES
% <code>SET yugakey 7</code><br>
"OK"<br>
% <code>INCR yugakey</code><br>
8<br>

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
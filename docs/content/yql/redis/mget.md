---
title: MGET
---

## SYNOPSIS
<code>MGET key [key ...]</code><br>
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

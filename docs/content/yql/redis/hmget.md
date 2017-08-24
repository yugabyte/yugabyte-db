---
title: HMGET
---

## SYNOPSIS
<code>HMGET key field [field ...]</code><br>
This command is to fetch one or more values for the given fields of the hash that is associated with the given <code>key</code>.

<li>For every given <code>field</code>, (null) is returned if either <code>key</code> or <code>field</code> does not exist.</li>
<li>If <code>key</code> is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of string values of the fields in the same order that was requested.

## EXAMPLES
% <code>HMSET yugahash area1 "Africa" area2 "America"</code><br>
"OK"<br>
% <code>HMGET yugahash area1 area2 area_none</code><br>
1) "Africa"<br>
2) "America"<br>
3) (null)<br>

## SEE ALSO

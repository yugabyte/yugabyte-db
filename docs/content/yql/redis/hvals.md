---
title: HVALS
---

## SYNOPSIS
<code>HVALS key</code><br>
This command is to select all the values in the hash that is associated with the given <code>key</code>.

<li>If the <code>key</code> does not exist, an empty list is returned.</li>
<li>if the <code>key</code> is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of values in the specified hash.

## EXAMPLES
% <code>HMSET yugahash area1 "America" area2 "Africa"</code><br>
"OK"<br>
% <code>HVALS yugahash</code><br>
1) "America"<br>
2) "Africa"<br>

## SEE ALSO

---
title: HGET
---

## SYNOPSIS
<code>HGET key field</code><br>
This command is to fetch the value for the given <code>field</code> in the hash that is specified by the given <code>key</code>.

<li>If the given <code>key</code> or <code>field</code> does not exist, null is returned.</li>
<li>If the given <code>key</code> is associated a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns the value for the given <code>field</code>;

## EXAMPLES
% <code>HSET yugahash area1 "America"</code><br>
1<br>
% <code>HGET yugahash area1</code><br>
"America"<br>

## SEE ALSO

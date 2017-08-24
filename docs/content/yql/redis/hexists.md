---
title: HEXISTS
---

## SYNOPSIS
<code>HEXISTS key field</code><br>
This is a predicate to check whether or not the given <code>field</code> exists in the hash that is specified by the given <code>key</code>.
<li>If the given <code>key</code> and <code>field</code> exist, 1 is returned.</li>
<li>If the given <code>key</code> or <code>field</code> does not exist, 0 is returned.</li>
<li>If the given <code>key</code> is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns existence status as integer, either 1 or 0.

## EXAMPLES
% <code>HSET yugahash area1 "America"</code><br>
1<br>
% <code>HEXISTS yugahash area1</code><br>
1<br>
% <code>HEXISTS yugahash area_none</code><br>
0<br>

## SEE ALSO

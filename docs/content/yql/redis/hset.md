---
title: HSET
---

## SYNOPSIS
<code>HSET key field value</code><br>
This command is to set the data for the given <code>field</code> of the hash that is associated with the given <code>key</code> with the given <code>value</code>. If the <code>field</code> already exists in the hash, it is overwritten.

<li>If the given <code>key</code> does not exist, an associated hash is created, and the <code>field</code> and <code>value</code> are inserted.</li>
<li>If the given <code>key</code> is not associated with a hash, an error is raised.</li>

## RETURN VALUE
Returns 1 if a new field is inserted and 0 if an existing field is updated.

## EXAMPLES
% <code>HSET yugahash area1 "America"</code><br>
1<br>
% <code>HSET yugahash area1 "North America"</code><br>
0<br>
% <code>HGET yugahash area1</code><br>
"North America"<br>

## SEE ALSO

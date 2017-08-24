---
title: HMSET
---

## SYNOPSIS
<code>HMSET key field value [field value ...]</code><br>
This command is to set the data for the given <code>field</code> with the given <code>value</code> in the hash that is specified by <code>key</code>.
<li>If the given <code>field</code> already exists in the specified hash, this command overwrites the existing value with the given <code>value</code>;
<li>If the given <code>key</code> does not exist, a new hash is created for the <code>key</code>, and the given values are inserted to the associated given fields.</li>
<li>If the given <code>key</code> is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
% <code>HMSET yugahash area1 "America" area2 "Africa"</code><br>
"OK"<br>
% <code>HGET yugahash area1</code><br>
"America"<br>

## SEE ALSO

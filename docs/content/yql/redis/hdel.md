---
title: HDEL
---

## SYNOPSIS
<code>HDEL key field [field ...]</code><br>
This command is to remove the given <code>fields</code> from the hash that is associated with the given <code>key</code>.

<li>If the given <code>key</code> does not exist, it is characterized as an empty hash, and 0 is returned for no elements are removed.</li>
<li>If the given <code>key</code> is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns the number of existing fields in the hash that were removed by this command.

## EXAMPLES
% <code>HSET yugahash moon "Moon"</code><br>
1<br>
% <code>HDEL yugahash moon</code><br>
1<br>
% <code>HDEL yugahash moon</code><br>
0<br>

## SEE ALSO

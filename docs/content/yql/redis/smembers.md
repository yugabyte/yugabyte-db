---
title: SMEMBERS
---
## SYNOPSIS
<code>SMEMBERS key</code><br>
This command selects all members of the set that is associated with the given <code>key</code>.
<li>If <code>key</code> is associated with a value that is not a set, an error is raised.</li>
<li>If <code>key</code> does not exist, no value is returned.</li>

## RETURN VALUE
Returns all members of the given set.

## EXAMPLES
% <code>SADD yuga_world "Africa"</code><br>
1<br>
% <code>SADD yuga_world "America"</code><br>
1<br>
% <code>SMEMBERS yuga_world</code><br>
1) "Africa"<br>
2) "America"<br>

## SEE ALSO

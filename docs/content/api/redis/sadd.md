---
title: SADD
---
## SYNOPSIS
<code><b>SADD key value [value ...]</b></code><br>
This command is to add one or more given values to the set that is associated with the given <code>key</code>.
<li>If the <code>key</code> does not exist, a new set is created, and members are added with the given values.
<li>If the <code>key</code> is associated with a value that is not a set, an error is raised.</li>
<li>If a specified <code>value</code> already exists in the given set, that <code>value</code> is ignored and not counted toward the total of newly added members.</li>

## RETURN VALUE
Returns the number of new members that were added by this command not including the duplicates.

## EXAMPLES
% <code>SADD yuga_world "Africa"</code><br>
% 1<br>
% <code>SADD yuga_world "America"</code><br>
% 1<br>
% <code>SMEMBERS yuga_world</code><br>
% 1) "Africa"<br>
% 2) "America"<br>

## SEE ALSO
[`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)

---
title: SISMEMBER
---
## SYNOPSIS
<code>SISMEMBER key member_value</code><br>
This command is a predicate for whether or not a value is a member of a set that is associated with the given  <code>key</code>.
<li>If the <code>key</code> is associated with a value that is not a set, an error is raised.</li>
<li>If the <code>key</code> does not exist, its associated set is empty, and the command returns 0.</li>
<li>If the <code>member</code> belongs to the given set, an integer of 1 is returned.</li>

## RETURN VALUE
Returns 1 if the specified member exists. Returns 0 otherwise.

## EXAMPLES
% <code>SADD yuga_world "America"</code><br>
% 1<br>
% <code>SISMEMBER yuga_world "America"</code><br>
% 1<br>
% <code>SISMEMBER yuga_world "Moon"</code><br>
% 0<br>

## SEE ALSO

---
title: SREM
---

## SYNOPSIS
<code>SREM key value [value ...] </code><br>
This command removes one or more specified members from the set that is associated with the given <code>key</code>.
<li>If the <code>key</code> does not exist, the associated set is an empty set, and the return value is zero.</li>
<li>If the <code>key</code> is associated with a value that is not a set, an error is raised.</li>
<li>If a specified <code>value</code> does not exist in the given set, that <code>value</code> is ignored and not counted toward the total of removed members.</li>

## RETURN VALUE
Returns the total number of existed members that were removed from the set.

## EXAMPLES
% <code>SADD yuga_world "America"</code><br>
% 1<br>
% <code>SADD yuga_world "Moon"</code><br>
% 1<br>
% <code>SREM yuga_world "Moon"</code><br>
% 1<br>
% <code>SREM yuga_world "Moon"</code><br>
% 0<br>

## SEE ALSO

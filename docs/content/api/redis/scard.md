---
title: SCARD
---

## SYNOPSIS
<code><b>SCARD key</b></code><br>
This command is to find the cardinality of the set that is associated with the given <code>key</code>. Cardinality is the number of elements in a set.
<li>If the <code>key</code> does not exist, 0 is returned.</li>
<li>If the <code>key</code> is associated with a non-set value, an error is raised.</li>

## RETURN VALUE
Returns the cardinality of the set.

## EXAMPLES
% <code>SADD yuga_world "America"</code><br>
1<br>
% <code>SADD yuga_world "Asia"</code><br>
1<br>
% <code>SCARD yuga_world</code><br>
2<br>

## SEE ALSO
[`sadd`](../sadd/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)

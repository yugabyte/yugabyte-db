---
title: SREM
weight: 2310
---

## SYNOPSIS
<b>`SREM key value [value ...]`</b><br>
This command removes one or more specified members from the set that is associated with the given `key`.
<li>If the `key` does not exist, the associated set is an empty set, and the return value is zero.</li>
<li>If the `key` is associated with a value that is not a set, an error is raised.</li>
<li>If a specified `value` does not exist in the given set, that `value` is ignored and not counted toward the total of removed members.</li>

## RETURN VALUE
Returns the total number of existed members that were removed from the set.

## EXAMPLES
```
$ SADD yuga_world "America"
1
$ SADD yuga_world "Moon"
1
$ SREM yuga_world "Moon"
1
$ SREM yuga_world "Moon"
0
```

## SEE ALSO
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/)

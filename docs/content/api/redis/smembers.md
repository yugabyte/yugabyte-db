---
title: SMEMBERS
weight: 230
---
## SYNOPSIS
<b>`SMEMBERS key`</b><br>
This command selects all members of the set that is associated with the given `key`.
<li>If `key` is associated with a value that is not a set, an error is raised.</li>
<li>If `key` does not exist, no value is returned.</li>

## RETURN VALUE
Returns all members of the given set.

## EXAMPLES
```
$ SADD yuga_world "Africa"
1
$ SADD yuga_world "America"
1
$ SMEMBERS yuga_world
1) "Africa"
2) "America"
```

## SEE ALSO
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)

---
title: SCARD
---

## SYNOPSIS
<b>`SCARD key`</b><br>
This command finds the cardinality of the set that is associated with the given `key`. Cardinality is the number of elements in a set.
<li>If the `key` does not exist, 0 is returned.</li>
<li>If the `key` is associated with a non-set value, an error is raised.</li>

## RETURN VALUE
Returns the cardinality of the set.

## EXAMPLES
```
$ SADD yuga_world "America"
1
$ SADD yuga_world "Asia"
1
$ SCARD yuga_world
2
```

## SEE ALSO
[`sadd`](../sadd/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)

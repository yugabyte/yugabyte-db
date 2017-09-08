---
title: SADD
---
## SYNOPSIS
<b>`SADD key value [value ...]`</b><br>
This command is to add one or more given values to the set that is associated with the given `key`.
<li>If the `key` does not exist, a new set is created, and members are added with the given values.
<li>If the `key` is associated with a value that is not a set, an error is raised.</li>
<li>If a specified `value` already exists in the given set, that `value` is ignored and not counted toward the total of newly added members.</li>

## RETURN VALUE
Returns the number of new members that were added by this command not including the duplicates.

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
[`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)

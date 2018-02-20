---
title: HGETALL
weight: 2130
---

## SYNOPSIS
<b>`HGETALL key`</b><br>
This command fetches the full content of all fields and all values of the hash that is associated with the given `key`.

<li>If the given `key` does not exist, and empty list is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of fields and values.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "Africa"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HSET yugahash area2 "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HGETALL yugahash
```
```sh
1) area1
2) "Africa"
3) area2
4) "America"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)

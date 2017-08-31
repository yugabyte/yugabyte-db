---
title: HGETALL
---

## SYNOPSIS
<code><b>HGETALL key</b></code><br>
This command is to fetch the full content of all fields and all values of the hash that is associated with the given <code>key</code>.

<li>If the given <code>key</code> does not exist, and empty list is returned.</li>
<li>If the given <code>key</code> is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of fields and values.

## EXAMPLES
% <code>HSET yugahash area1 "Africa"</code><br>
1<br>
% <code>HSET yugahash area2 "America"</code><br>
1<br>
% <code>HGETALL yugahash</code><br>
1) area1<br>
2) "Africa"<br>
3) area2<br>
4) "America"<br>

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)

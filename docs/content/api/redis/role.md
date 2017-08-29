---
title: ROLE
---
Early Releases: YugaByte only have master nodes for Redis services.

## SYNOPSIS
<code><b>ROLE</b></code><br>
This command is to provide information of a Redis instance, such as its role, its state of replication, its slaves, or its master. Roles are either "master", "slave", or "sentinel".
<li>Information of a master instance may include the following.
  <ol>
  <li>"master"</li>
  <li>An integer that represents state of replication</li>
  <li>An array of connected slaves { IP address, IP port, State of replication }</li>
  </ol>
</li>

<li>Information of a slave instance may include the following.
  <ol>
  <li>"slave"</li>
  <li>Master IP address</li>
  <li>Master IP port</li>
  <li>Connection state that is either "disconnected", "connecting", "sync", or "connected"</li>
  <li>An integer that represents state of replication</li>
  </ol>
</li>

<li>Information of a sentinel instance may include the following.
  <ol>
  <li>"sentinel"</li>
  <li>An array of master names.</li>
  </ol>
</li>

## RETURN VALUE
Returns an array of values.

## EXAMPLES
% <code>ROLE</code><br>
1) "master"<br>
2) 0<br>
3) 1) 1) "127.0.0.1"<br>
      2) "9200"<br>
      3) "0"<br>
   2) 1) "127.0.0.1"<br>
      2) "9201"<br>
      3) "0"<br>

## SEE ALSO
[`auth`](/yql/redis/auth/), [`config`](/yql/redis/config/)

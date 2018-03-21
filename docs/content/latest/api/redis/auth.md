---
title: AUTH
linkTitle: AUTH
description: AUTH
menu:
  latest:
    parent: api-redis
    weight: 2020
aliases:
  - api/redis/auth
---
Early Releases: Not supported. All `AUTH` requests with any password would be accepted as valid.

## SYNOPSIS
<b>`AUTH password`</b><br>
This command authenticates a client connection to YugaByte Redis Server.
<li>YugaByte server can be setup to require a password before allowing a client to execute Redis commands. The password verification can be enforced by setting the requirepass option in server configuration file.</li>
<li>If the given `password` matches with the server configured password, server returns the status string "OK" and begins processing commands from the authenticated client.</li>
<li>If the given `password` does not match with the server configured password, an error is raised</li>

## RETURN VALUE
Returns a status string.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ AUTH "yugapass"
```
```sh
"OK"
```

## SEE ALSO
[`config`](../config/)

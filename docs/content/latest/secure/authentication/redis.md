
## Overview

YEDIS authentication is based on passwords. Each client connecting using the YEDIS API should provide a valid password in order to execute any command successfully.

**NOTE:** YEDIS implements a password-only authentication scheme. From the [Redis security docs page ("Authentication feature" section)](https://redis.io/topics/security), Open-source Redis does not try to implement Access Control, it provides a tiny layer of authentication that is optionally turned on editing the redis.conf file.


## 1. Enable YEDIS authentication

You can enable access control to enforce password based authentication in YEDIS API using the [CONFIG](../../api/yedis/config/) command.

To do so, connect to the cluster using `redis-cli` and run the following command:
<div class='copy separator-gt'>
```sql
127.0.0.1:6379> CONFIG SET requirepass "password"
OK
```
</div>

## 2. Connect with redis-cli

Next exit `redis-cli`, connect to the cluster again using `redis-cli` and run the `PING` command (or any other command).
<div class='copy separator-gt'>
```sql
127.0.0.1:6379> PING
(error) NOAUTH PING: Authentication required.
```
</div>

You would need to authenticate the client (`redis-cli` in this case) by running the [AUTH](../../api/yedis/auth/) command:
<div class='copy separator-gt'>
```sql
127.0.0.1:6379> AUTH password
OK
```
</div>

Subsequently, running any command would succeed:
<div class='copy separator-gt'>
```sql
127.0.0.1:6379> PING
PONG
```
</div>


## 3. Changing authentication credentials

YEDIS allows for multiple passwords (up to 2) to be accepted. This enables performing a graceful change of password without experiencing any application outage. Note that this requires knowing the old password.

Let us assume that the old password is `old-password` and the new password we intend to change it to is `new-password`. The preferred sequence is:

- Add a new password

This can be done as follows:

```
CONFIG SET requirepass "old-password,new-password"
```

This enables connecting to the database using both passwords.

- Change password used by the application tier

This would involve changing the config or pushing an updated binary to the application tier so that it now connects using `new-password`.

- Drop old password

This can be done by running the following command:

```
CONFIG SET requirepass "new-password"
```

---
title: Create login profiles
headerTitle: Create and configure login profiles in YSQL
linkTitle: Create login profiles
description: Create and configure login profiles in YugabyteDB
headcontent: Prevent brute force cracking with login profiles
menu:
  preview:
    identifier: ysql-create-profiles
    parent: enable-authentication
    weight: 725
type: docs
---

To enhance the security of your database, you can enable login profiles to lock accounts after a specified number of login attempts. This prevents brute force exploits.

When enabled, database administrators with superuser privileges can assign login profiles to roles.

A login profile consists of two parameters:

- Number of failed attempts.
- A lockout time period.

The number of failed attempts increments by one every time authentication fails during login. If the number of failed attempts is greater than the preset limit, then the account is locked.

If authentication is successful, or a locked account is unlocked, the number of failed attempts is reset to 0.

## Enable login profiles

### Start local clusters

To enable login profiles in your local YugabyteDB clusters, include the YB-TServer `--ysql_enable_profile` flag with the [yugabyted start](../../../reference/configuration/yugabyted/#start) command `--tserver_flags` flag, as follows:

```sh
./bin/yugabyted start --tserver_flags="ysql_enable_profile=true"
```

### Start YB-TServer services

To enable YSQL authentication in deployable YugabyteDB clusters, you need to start your YB-TServer services using the [--ysql_enable_profile](../../../reference/configuration/yb-tserver/#ysql-enable-profile) flag. Your command should look similar to the following:

```sh
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --ysql_enable_auth=true \
  --ysql_enable_profile=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can also enable YSQL login profiles by adding the `--ysql_enable_profile=true` to the YB-TServer configuration file (`tserver.conf`). For more information, refer to [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

## Locked behaviour

A profile can be locked indefinitely or for a specific interval. The `role_profile` has two states for the different LOCK behaviour:

- L (LOCKED): Role is locked indefinitely.
- T (LOCKED(TIMED)): Role is locked until a certain timestamp

A role is moved to the LOCKED(TIMED) state when the number of consecutive failed attempts exceeds the limit. The interval (in seconds) to lock the role is read from `pg_yb_profile.prfpasswordlocktime`. The interval is added to the current timestamp and stored in `pg_yb_role_profile.pg_yb_rolprflockeduntil`. Login attempts by the role before `pg_yb_role_profile.pg_yb_rolprflockeduntil` will fail. If the column is NULL, then it is moved to LOCKED state instead.

When the role successfully logs in after `pg_yb_role_profile.pg_yb_rolprflockeduntil`, the role is moved to the OPEN state, and is allowed to log in. Failed attempts after the lock time out period don't modify `pg_yb_role_profile.pg_yb_rolprflockeduntil`.

### Complete lockout

If you lock out all roles including admin roles, you must restart the cluster with the `--ysql_enable_profile` flag disabled.

While disabling login profiles allows users back in, you won't be able to change any profile information, as profile commands can't be run when the profile flag is off.

To re-enable accounts, do the following:

1. Restart the cluster without profiles enabled.
1. Create a new superuser.
1. Restart the cluster with profiles enabled.
1. Connect as the new superuser and issue the profile commands.

## Manage login profiles

When profiles are enabled, you can manage login profiles using the following commands:

- CREATE PROFILE
- DROP PROFILE
- ALTER ROLE

Only superusers can create or drop profiles, and assign profiles to roles.

To revoke superuser privileges from a user so that they can no longer change privileges for other roles, do the following:

```sql
ALTER USER db_admin WITH NOSUPERUSER;
```

To create a profile, do the following:

```sql
CREATE PROFILE myprofile LIMIT 
FAILED_LOGIN_ATTEMPTS <number> 
[PASSWORD_LOCK_TIME <# of days>];
```

To drop a profile, do the following:

```sql
DROP PROFILE myprofile;
```

To attach a role to a profile, do the following:

```sql
ALTER ROLE myuser PROFILE myprofile;
```

To detach a role from a profile, do the following:

```sql
ALTER ROLE myuser NOPROFILE;
```

Note that the association between a role and its profile should be removed using `ALTER ROLE ... NOPROFILE` before dropping a role.

### Lock and unlock roles

To unlock a role that has been locked out, do the following:

```sql
ALTER ROLE myuser ACCOUNT UNLOCK;
```

To lock a role so that it can't log in, do the following:

```sql
ALTER ROLE myuser ACCOUNT LOCK;
```

### View profiles

The `pg_yb_profile` system table lists profiles and their attributes.

To view profiles, enter the following command:

```sql
SELECT * FROM pg_yb_profile;
```

You should see output similar to the following:

```output

```

Run the following meta-command to verify the profiles:

```sql
yugabyte=# \dgP
```

You should see output similar to the following:

```output
                                       List of roles
  Role name   |                         Attributes                         |   Member of
--------------+------------------------------------------------------------+---------------
 db_admin     | Cannot login                                               | {engineering}
 developer    | Cannot login                                               | {engineering}
 engineering  | Cannot login                                               | {}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 qa           | Cannot login                                               | {engineering}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## Limitations and caveats

- A profile can't be modified using `ALTER PROFILE`. If a profile needs to be modified, create a new profile. ALTER functionality will be implemented in the future.
- Currently a role is locked indefinitely unless an admin unlocks the role. Locking a role for a specific period of time will be implemented in the future.
- Login profiles are only applicable to challenge-response authentication methods. YugabyteDB also supports authentication methods that are not challenge-response, and login profiles are ignored for these methods as the authentication outcome has already been determined. The authentication methods are as follows:
  - Reject
  - ImplicitReject
  - Trust
  - YbTserverKey
  - Peer

  For more information on these authentication methods, refer to [Client Authentication](https://www.postgresql.org/docs/11/client-authentication.html) in the PostgreSQL documentation.

- If the cluster SSL mode is allow or prefer, a single user login attempt can trigger two failed login attempts.
- The \h and \dg meta commands do not currently provide information about PROFILE and ROLE PROFILE catalog objects.

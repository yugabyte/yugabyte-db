---
title: Create login profiles
headerTitle: Create and configure login profiles in YSQL
linkTitle: Create login profiles
description: Create and configure login profiles in YugabyteDB
headcontent: Prevent brute force cracking with login profiles
menu:
  preview:
    identifier: ysql-login-profiles
    parent: enable-authentication
    weight: 725
type: docs
---

To enhance the security of your database, you can enable login profiles to lock accounts after a specified number of login attempts. This prevents brute force exploits.

When enabled, database administrators with superuser (or in YugabyteDB Managed, `yb_db_admin`) privileges can create login profiles and assign roles to the profiles.

There is no default profile for roles; you must explicitly assign all roles with login privileges to the profile if you want the policy to apply to all users. Users not associated with a profile continue to have unlimited login attempts.

When creating a profile, you must specify the number of failed attempts that are allowed before the account with the profile is locked.

The number of failed attempts increments by one every time authentication fails during login. If the number of failed attempts is equal to the preset limit, then the account is locked. For example, if the limit is 3, a user is locked out on the third failed attempt.

If authentication is successful, or if an administrator unlocks a locked account, the number of failed attempts resets to 0.

## Enable login profiles

### Start local clusters

To enable login profiles in your local YugabyteDB clusters, include the YB-TServer [--ysql_enable_profile](../../../reference/configuration/yb-tserver/#ysql-enable-profile) flag with the [yugabyted start](../../../reference/configuration/yugabyted/#start) command `--tserver_flags` flag, as follows:

```sh
./bin/yugabyted start --tserver_flags="ysql_enable_profile=true"
```

### Start YB-TServer services

To enable login profiles in deployable YugabyteDB clusters, you need to start your YB-TServer services using the `--ysql_enable_profile` flag. Your command should look similar to the following:

```sh
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --ysql_enable_auth=true \
  --ysql_enable_profile=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can also enable YSQL login profiles by adding the `--ysql_enable_profile=true` to the YB-TServer configuration file (`tserver.conf`).

For more information, refer to [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

## Manage login profiles

When profiles are enabled, you can manage login profiles using the following commands:

- `CREATE PROFILE`
- `DROP PROFILE`
- `ALTER ROLE`

Only superusers can create or drop profiles, and assign profiles to roles.

### Create and drop profiles

To create a profile, do the following:

```sql
CREATE PROFILE myprofile LIMIT
  FAILED_LOGIN_ATTEMPTS <number>;
  [PASSWORD_LOCK_TIME <days>];
```

Note that `PASSWORD_LOCK_TIME` is optional, and timed locking is not currently supported.

You can drop a profile as follows:

```sql
DROP PROFILE myprofile;
```

### Assign roles to profiles

You can assign a role to a profile as follows:

```sql
ALTER ROLE myuser PROFILE myprofile;
```

You can remove a role from a profile as follows:

```sql
ALTER ROLE myuser NOPROFILE;
```

Note that you should remove the association between a role and its profile using `ALTER ROLE ... NOPROFILE` before dropping a role.

### Lock and unlock roles

You can unlock a role that has been locked out as follows:

```sql
ALTER ROLE myuser ACCOUNT UNLOCK;
```

You can lock a role so that it can't log in as follows:

```sql
ALTER ROLE myuser ACCOUNT LOCK;
```

### Recover from complete lockout

If you lock out all roles including administrator roles, you must restart the cluster with the `--ysql_enable_profile` flag disabled.

While disabling login profiles allows users back in, you won't be able to change any profile information, as profile commands can't be run when the profile flag is disabled.

To re-enable accounts, do the following:

1. Restart the cluster without profiles enabled.
1. Create a new superuser.
1. Restart the cluster with profiles enabled.
1. Connect as the new superuser and issue the profile commands to unlock the accounts.

<!--## Timed locked behaviour

A profile can be locked indefinitely or for a specific interval. The `pg_yb_role_profile` has two states for the different LOCK behaviour:

- L (LOCKED): Role is locked indefinitely.
- T (LOCKED(TIMED)): Role is locked for a specified duration.

A role is moved to the LOCKED(TIMED) state when the number of consecutive failed attempts exceeds the limit. The interval (in seconds) to lock the role is read from `pg_yb_profile.prfpasswordlocktime`. The interval is added to the current timestamp and stored in `pg_yb_role_profile.pg_yb_rolprflockeduntil`. Login attempts by the role before `pg_yb_role_profile.pg_yb_rolprflockeduntil` will fail. If the column is NULL, then it is moved to LOCKED state instead.

When the role successfully logs in after `pg_yb_role_profile.pg_yb_rolprflockeduntil`, the role is moved to the OPEN state, and is allowed to log in. Failed attempts after the lock time out period don't modify `pg_yb_role_profile.pg_yb_rolprflockeduntil`. -->

## View profiles

The `pg_yb_profile` table lists profiles and their attributes.

To view profiles, execute the following statement:

```sql
SELECT * FROM pg_yb_profile;
```

You should see output similar to the following:

```output
  prfname  | prfmaxfailedloginattempts | prfpasswordlocktime 
-----------+---------------------------+---------------------
 myprofile |                         3 |                   0
(1 row)
```

The following table describes the columns and their values:

| COLUMN | TYPE | DESCRIPTION |
| :---- | :--- | :---------- |
| `prfname` | name | Name of the profile. Must be unique. |
| `prfmaxfailedloginattempts` | int | Maximum number of failed attempts allowed. |
| `prfpasswordlocktime` | int | Interval in seconds to lock the account. NULL implies that the role will be locked indefinitely. |

### View role profile information

The `pg_yb_role_profile` table lists role profiles and their attributes.

To view profiles, execute the following statement:

```sql
SELECT * FROM pg_yb_role_profile;
```

You should see output similar to the following:

```output
 rolprfrole | rolprfprofile | rolprfstatus | rolprffailedloginattempts | rolprflockeduntil 
------------+---------------+--------------+---------------------------+-------------------
      13287 |         16384 | o            |                         0 | 
(1 row)
```

The following table describes the columns and their values:

| COLUMN | TYPE | DEFAULT | DESCRIPTION |
| :----- | :--- | :------ | :---------- |
| `rolprfrole` | OID | | OID of the row in PG_ROLE
| `rolprfprofile` | OID | | OID of the row in PROFILE
| `rolprfstatus` | char | o | The status of the account, as follows:<ul><li>`o` (OPEN); allowed to log in.</li><li>`t` (LOCKED(TIMED)); locked for a duration of the timestamp stored in `rolprflockeduntil`. (Note that timed locking is not supported.)</li><li>`l` (LOCKED); locked indefinitely and can only be unlocked by the admin.</li></ul>
| `rolprffailedloginattempts` | int | 0 | Number of failed attempts by this role.
| `rolprflockeduntil` | timestamptz | Null | If `rolprfstatus` is `t`, the duration that the role is locked. Otherwise, the value is NULL and not used.

<!-- When login profiles are enabled, you can display these columns in the `pg_roles` table by running the following [meta-command](../../../admin/ysqlsh-meta-commands/):

```sql
yugabyte=# \dgP
```
-->

## Limitations and caveats

- A profile can't be modified using `ALTER PROFILE`. If a profile needs to be modified, create a new profile.
- Currently a role is locked indefinitely unless an administrator unlocks the role.
- Login profiles are only applicable to challenge-response authentication methods. YugabyteDB also supports authentication methods that are not challenge-response, and login profiles are ignored for these methods as the authentication outcome has already been determined. The authentication methods are as follows:
  - Reject
  - ImplicitReject
  - Trust
  - YbTserverKey
  - Peer

  For more information on these authentication methods, refer to [Client Authentication](https://www.postgresql.org/docs/11/client-authentication.html) in the PostgreSQL documentation.

- If the cluster SSL mode is `allow` or `prefer`, a single user login attempt can trigger two failed login attempts. For more information on SSL modes in PostgreSQL, refer to [SSL Support](https://www.postgresql.org/docs/11/libpq-ssl.html) in the PostgreSQL documentation.
- The `\h` and `\dg` meta commands do not currently provide information about PROFILE and ROLE PROFILE catalog objects.

Enhancements to login profiles are tracked in GitHub issue [#15676](https://github.com/yugabyte/yugabyte-db/issues/15676).

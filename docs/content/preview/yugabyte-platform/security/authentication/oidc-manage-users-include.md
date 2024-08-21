<!--
+++
private = true
+++
-->

After OIDC-based authentication is configured, an administrator can manage users as follows:

- In the universe, add database users or roles.

  You need to add the users and roles that will be used to authenticate to the database. The role must be assigned the appropriate permissions in advance. Users will use their database user/role as their username credential along with their JWT as the password when connecting to the universe.

  For information on managing users and roles in YugabyteDB, see [Manage users and roles](../../../../secure/authorization/create-roles/).

- In YugabyteDB Anywhere, create YBA users.

  Create a user in YugabyteDB Anywhere for each user who wishes to sign in to YBA to obtain their JWT.

  To view their JWT, YBA users can sign in to YugabyteDB Anywhere, click the **User** icon at the top right, select **User Profile**, and click **Fetch OIDC Token**.

  This is not required if you enabled the **Display JWT token on login** option in the YBA OIDC configuration, as any database user can copy the JWT from the YBA landing page without signing in to YBA.

  For information on how to add YBA users, see [Create, modify, and delete users](../../../administer-yugabyte-platform/anywhere-rbac/#create-modify-and-delete-users).

## Using your JWT

If the administrator has enabled the **Display JWT token on login** setting, you can obtain your token from the YugabyteDB Anywhere landing page. Click **Fetch JSON Web Token**; you are redirected to the IdP to enter your credentials as required. You are then redirected back to YugabyteDB Anywhere, where your token is displayed, along with the expiration time of the token.

Use the token as your password to access the database. Your username will match the database username/role that was assigned to you by your administrator.
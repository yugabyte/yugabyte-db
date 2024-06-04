---
title: Manage runtime configuration settings
headerTitle: Manage runtime configuration settings
linkTitle: Manage runtime configuration
description: Manage your universes by setting runtime configuration keys based on different scopes.
menu:
  v2.18_yugabyte-platform:
    identifier: manage-runtime-config
    parent: administer-yugabyte-platform
    weight: 50
type: docs
---

YugabyteDB Anywhere has a runtime configuration mechanism that is fully compatible with the existing [typesafe config](https://github.com/lightbend/config) format and has the following additional properties:

- Runtime/Dynamic: The configuration keys can be set or changed at runtime via the API.
- Persistent: The site-specific configuration changes won't be lost in an install.
- Backwards compatibility: Fully compatible with the existing mechanism with default values residing in the file `application.conf` or `reference.conf`.
- Scoped: Configuration can be applied to a smaller scope, for example, even a single universe can be configured to have certain features.
- Hierarchical: Scopes are nested and allow for overriding outer scope values in the inner scope.

Currently, the scopes can be categorized as follows:

| Scope | Description | User Access |
|:--- |:--- | :--- |
| Global | Applies to the entire platform, persists across installs and any value defined in this scope overrides the defaults. | Super Admin |
| Customer | Applies to a specific customer, persists across installs and any value defined in this scope overrides defaults in Global scope.| Super Admin, Admin |
| Provider | Applies to a specific provider, persists across installs and any value defined in this scope overrides defaults in Customer scope. | Super Admin, Admin |
| Universe | Applies to a specific universe, persists across installs and any value defined in this scope overrides defaults in Customer scope. | Super Admin, Admin |

For more details about the flags and their scopes, refer to the [list of supported runtime configuration flags](https://github.com/yugabyte/yugabyte-db/blob/2.17.3/managed/RUNTIME-FLAGS.md)

## Edit or reset runtime configuration keys

To modify the runtime configuration keys, in YugabyteDB Anywhere, navigate to **Admin** > **Advanced** to display the runtime configuration settings, similar to the following illustration:

![Runtime configuration overview](/images/ee/runtime-config-overview.png)

Note that only a Super Admin user has edit or reset access for Global configuration level keys, an Admin user has access for Customer, Provider, and Universe configuration level keys, and all other users only have read-only access.

### Example

To modify the `Enforce Auth` runtime configuration key (applicable only at Global or Customer scope), perform the following steps:

1. From your YugabyteDB Anywhere UI, navigate to **Admin** > **Advanced** and select the **Global Configuration** or **Customer Configuration** tab.
1. In the **Search** bar, enter "Enforce Auth".
1. Click **Actions** and choose **Edit Configuration**.
1. Change the **Config value** to "True" and click **Save**.
1. To verify the change, navigate to **Universes**, click **Create Universe**, and verify that for the  **Authentication Settings** section, "Enable YSQL Auth" is not be displayed and you are must enter a password in the **YSQL Password** and **Confirm Password** fields.

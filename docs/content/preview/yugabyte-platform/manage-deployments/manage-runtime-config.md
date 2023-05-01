---
title: Manage Runtime configuration
headerTitle: Manage Runtime configuration
linkTitle: Manage Runtime configuration
description: Manage your universes by setting runtime configuration keys based on different scopes.
menu:
  preview_yugabyte-platform:
    identifier: manage-runtime-config
    parent: manage-deployments
    weight: 100
type: docs
---

YugabyteDB Anywhere has a configuration mechanism that is fully compatible with the existing [typesafe config](https://github.com/lightbend/config) format and has the following additional properties:

- Runtime/Dynamic: The configuration keys can be set or changed at runtime via the API.
- Persistent: The site specific configuration changes won't be lost in an install.
- Backwards compatibility: Fully compatible with existing mechanism with default values residing in the file `application.conf` or `reference.conf`.
- Scoped: Configuration can be applied to a smaller scope, for example, even a single universe can be configured to have certain features.
- Hierarchical: Scopes are nested and allow for overriding outer scope values in inner scope.

Currently, the scopes can be categorized as follows:

| Scope | Description |
|:--- |:---|
| Global | Applies to the entire platform, persists across installs and any value defined in this scope overrides the defaults. |
| Customer | Applies to a specific customer, persists across installs and any value defined in this scope overrides defaults in Global scope.|
| Provider | Applies to a specific provider, persists across installs and any value defined in this scope overrides defaults in Customer scope. |
| Universe | Applies to a specific universe, persists across installs and any value defined in this scope overrides defaults in Customer scope. |

## Edit or reset runtime configuration keys

To modify the runtime configuration keys, navigate to your YugabyteDB Anywhere UI and click **Admin** > **Advanced** to open the different configuration scopes similar to the following illustration:

![Runtime configuration overview](/images/ee/runtime-config-overview.png)

Note that only a Super Admin user has edit or reset access for Global runtime configuration keys, an Admin user has access for Customer, Provider, and Universe level keys, and all other users will have read-only access.

### Example

Consider you want to modify the runtime configuration key: `Enforce Auth` (applicable only at Global or Customer scope). Perform the following steps:

1. From your YugabyteDB Anywhere UI, navigate to **Admin** > **Advanced** and click the **Global Configuration** or **Customer Configuration** tab.
1. From the **Search** bar, enter "Enforce Auth".
1. From the **Actions** dropdown, select **Edit Configuration** and change the **Config value** to "True".
1. Navigate to **Universes**, click **Create Universe**, and verify that for the  **Authentication Settings** section, "Enable YSQL Auth" will not be displayed and you are forced to enter a password for **YSQL Password** and **Confirm Password** fields.

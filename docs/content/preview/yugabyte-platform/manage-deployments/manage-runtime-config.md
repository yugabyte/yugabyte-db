---
title: Manage Runtime configuration
headerTitle: Manage Runtime configuration
linkTitle: Manage Runtime configuration
description: Migrate your YugabyteDB universes and YugabyteDB Anywhere from Helm 2 to Helm 3.
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
- Backwards compatibility: Fully compatible with existing mechanism with default values residing in `application.conf` or `reference.conf`.
- Scoped: Configuration can be applied to smaller scope, for example, even a single universe can be configured to have certain features.
- Hierarchical: Scopes are nested and allow for overriding outer scope values in inner scope.

Currently, the scopes can be categorized as follows:

- **Application**: This is the existing file based (`reference.conf/application.conf`) static configuration. This scope is global in nature and cannot be defined for a single universe.

- **Global**: This scope also applies to the entire platform like **Application** scope, persists across installs and any value defined here overrides defaults in Application scope.

- **Customer** - This scope applies to a specific customer, persists across installs and any value defined here overrides defaults in **Global** scope.

- **Provider**: This scope applies to a specific provider, persists across installs and any value defined here overrides defaults in **Customer** scope.

- **Universe**: This scope applies to a specific universe, persists across installs and any value defined here overrides defaults in **Customer** scope.

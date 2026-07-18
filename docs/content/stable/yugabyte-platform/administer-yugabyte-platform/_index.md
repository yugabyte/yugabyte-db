---
title: Administer YugabyteDB Anywhere
headerTitle: Administer YugabyteDB Anywhere
linkTitle: Administer
description: Administer YugabyteDB Anywhere
headcontent: Manage your YugabyteDB Anywhere installation
aliases:
  - /stable/yugabyte-platform/administer-yp
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: administer-yugabyte-platform
    weight: 690
type: indexpage
---

For information on configuring alerts, health checks, and diagnostics reporting for universes ("call home"), refer to [Configure health check](../alerts-monitoring/set-up-alerts-health-check/#configure-health-check).

{{<index/block>}}

  {{<index/item
    title="Manage YugabyteDB Anywhere users and roles"
    body="Invite team members to your account and manage their access."
    href="anywhere-rbac/"
    icon="fa-thin fa-user">}}

  {{<index/item
    title="Configure YugabyteDB Anywhere authentication"
    body="Use LDAP or OIDC for authentication in YugabyteDB Anywhere."
    href="ldap-authentication/"
    icon="fa-thin fa-lock">}}

  {{<index/item
    title="Back up and restore YugabyteDB Anywhere"
    body="Back up and restore the YugabyteDB Anywhere server."
    href="back-up-restore-yba/"
    icon="fa-thin fa-arrow-down-to-bracket">}}

  {{<index/item
    title="Enable high availability"
    body="Configure standby instances of YugabyteDB Anywhere."
    href="high-availability/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Manage runtime configuration settings"
    body="Customize YugabyteDB Anywhere by changing default settings for the application, universes, and providers."
    href="manage-runtime-config/"
    icon="fa-thin fa-gear">}}

  {{<index/item
    title="Shutdown"
    body="Shut YugabyteDB Anywhere down gracefully."
    href="shutdown/"
    icon="fa-thin fa-power-off">}}

{{</index/block>}}

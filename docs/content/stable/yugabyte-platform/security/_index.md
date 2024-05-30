---
title: YugabyteDB Anywhere Security
headerTitle: Security
linkTitle: Security
description: Secure YugabyteDB Anywhere and YugabyteDB universes.
image: /images/section_icons/index/secure.png
headcontent: Secure YugabyteDB Anywhere and your YugabyteDB universes.
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: security
weight: 660
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Security checklist"
    body="Address security requirements for YugabyteDB Anywhere and YugabyteDB universes."
    href="security-checklist-yp/"
    icon="/images/section_icons/secure/checklist.png">}}

  {{<index/item
    title="Configure ports"
    body="Configure YugabyteDB ports for security purposes."
    href="customize-ports/"
    icon="/images/section_icons/index/secure.png">}}

  {{<index/item
    title="Database authentication"
    body="Configure client authentication for your universes."
    href="authentication/"
    icon="/images/section_icons/secure/authentication.png">}}

  {{<index/item
    title="Database authorization"
    body="Manage universe users and roles."
    href="authorization-platform/"
    icon="/images/section_icons/secure/authorization.png">}}

  {{<index/item
    title="Enable encryption in transit (TLS)"
    body="Enable encryption in transit using TLS to secure data in transit."
    href="enable-encryption-in-transit/"
    icon="/images/section_icons/secure/tls-encryption.png">}}

  {{<index/item
    title="Enable encryption at rest"
    body="Enable encryption at rest to protect data in storage."
    href="enable-encryption-at-rest/"
    icon="/images/section_icons/secure/tls-encryption.png">}}

  {{<index/item
    title="Create a KMS configuration"
    body="Configure a key management service with a customer managed key to use for encryption at rest."
    href="create-kms-config/aws-kms/"
    icon="/images/section_icons/secure/tls-encryption/server-to-server.png">}}

{{</index/block>}}

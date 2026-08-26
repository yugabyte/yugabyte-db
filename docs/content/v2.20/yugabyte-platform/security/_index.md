---
title: YugabyteDB Anywhere Security
headerTitle: Security
linkTitle: Security
description: Secure YugabyteDB Anywhere and YugabyteDB universes.
headcontent: Secure YugabyteDB Anywhere and your YugabyteDB universes
menu:
  v2.20_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: security
weight: 650
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Security checklist"
    body="Address security requirements for YugabyteDB Anywhere and YugabyteDB universes."
    href="security-checklist-yp/"
    icon="fa-thin fa-clipboard">}}

  {{<index/item
    title="Configure ports"
    body="Configure YugabyteDB ports for security purposes."
    href="customize-ports/"
    icon="fa-thin fa-network-wired">}}

  {{<index/item
    title="Database authentication"
    body="Configure client authentication for your universes."
    href="authentication/"
    icon="fa-thin fa-lock-keyhole">}}

  {{<index/item
    title="Database authorization"
    body="Manage universe users and roles."
    href="authorization-platform/"
    icon="fa-thin fa-user-group-crown">}}

  {{<index/item
    title="Enable encryption in transit (TLS)"
    body="Enable encryption in transit using TLS to secure data in transit."
    href="enable-encryption-in-transit/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Enable encryption at rest"
    body="Enable encryption at rest to protect data in storage."
    href="enable-encryption-at-rest/"
    icon="fa-thin fa-binary-lock">}}

  {{<index/item
    title="Create a KMS configuration"
    body="Configure a key management service with a customer managed key to use for encryption at rest."
    href="create-kms-config/aws-kms/"
    icon="fa-thin fa-key">}}

{{</index/block>}}

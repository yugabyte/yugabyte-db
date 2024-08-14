---
title: Security architecture overview
headerTitle: Security architecture overview
linkTitle: Security architecture
description: Review and overview of the YugabyteDB Aeon database security architecture and shared responsibility model.
image: /images/section_icons/index/secure.png
headcontent: YugabyteDB Aeon security architecture and shared responsibility model
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-security
weight: 800
type: indexpage
---

YugabyteDB Aeon is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on public cloud providers such as Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

YugabyteDB Aeon uses a shared responsibility model, where security and compliance is a shared responsibility between public cloud providers, Yugabyte, and YugabyteDB Aeon customers.

The YugabyteDB Aeon architecture is secure by default, and uses the following features to protect clusters and communication between clients and databases:

- encryption in transit
- encryption at rest
- limited network exposure
- authentication
- role-based access control for authorization

For information on how to configure the security features of clusters in YugabyteDB Aeon, refer to [Secure clusters in YugabyteDB Aeon](../cloud-secure-clusters/).

{{<index/block>}}

  {{<index/item
    title="Security architecture"
    body="Learn about the YugabyteDB Aeon security architecture."
    href="cloud-security-features/"
    icon="/images/section_icons/secure/checklist.png">}}

  {{<index/item
    title="Shared responsibility model"
    body="The YugabyteDB Aeon shared responsibility model for security."
    href="shared-responsibility/"
    icon="/images/section_icons/secure/grant-permissions.png">}}

{{</index/block>}}

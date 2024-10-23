---
title: Secure
headerTitle: Secure
linkTitle: Secure
description: Secure your deployment of YugabyteDB.
headcontent: Secure your deployment of YugabyteDB
image: fa-thin fa-building-lock
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Security checklist"
    body="Consider these security measures when deploying your YugabyteDB cluster."
    href="security-checklist/"
    icon="fa-thin fa-clipboard">}}

  {{<index/item
    title="Enable authentication"
    body="Enable authentication for all clients connecting to YugabyteDB."
    href="enable-authentication/"
    icon="fa-thin fa-lock-keyhole">}}

  {{<index/item
    title="Authentication methods"
    body="Choose the appropriate authentication mechanism."
    href="authentication/"
    icon="fa-thin fa-badge-check">}}

  {{<index/item
    title="Role-based access control"
    body="Manage users and roles, grant privileges, implement row-level security (RLS), and column-level security."
    href="authorization/"
    icon="fa-thin fa-user-group-crown">}}

  {{<index/item
    title="Encryption in transit"
    body="Secure and protect network communication using TLS."
    href="tls-encryption/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Encryption at rest"
    body="Secure and protect data on disk."
    href="encryption-at-rest/"
    icon="fa-thin fa-binary-lock">}}

  {{<index/item
    title="Column-level encryption"
    body="Encrypt sensitive data using per-column encryption at the application layer (using symmetric and asymmetric encryption)."
    href="column-level-encryption/"
    icon="fa-thin fa-table-cells-column-lock">}}

  {{<index/item
    title="Audit logging"
    body="Configure session- and object-level audit logging for security and compliance."
    href="audit-logging/"
    icon="fa-thin fa-calculator">}}

{{</index/block>}}

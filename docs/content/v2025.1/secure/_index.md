---
title: Secure
headerTitle: Secure
linkTitle: Secure
description: Secure your deployment of YugabyteDB.
headcontent: Secure your deployment of YugabyteDB
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
    body="Enable encryption in transit (using TLS) to secure and protect network communication."
    href="tls-encryption/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Encryption at rest"
    body="Enable encryption at rest to secure and protect data on disk."
    href="encryption-at-rest/"
    icon="fa-thin fa-binary-lock">}}

  {{<index/item
    title="Column-level encryption"
    body="Encrypt data present in columns containing sensitive data using per-column encryption at the application layer in YugabyteDB (using symmetric and asymmetric encryption)."
    href="column-level-encryption/"
    icon="fa-thin fa-table-cells-column-lock">}}

  {{<index/item
    title="Audit logging"
    body="Configure YugabyteDB's session-level and object-level audit logging for security and compliance."
    href="audit-logging/"
    icon="fa-thin fa-calculator">}}

  {{<index/item
    title="Vulnerability disclosure"
    body="Learn about Yugabyte vulnerability reporting."
    href="/preview/secure/vulnerability-disclosure-policy/"
    icon="fa-thin fa-shield-check">}}

{{</index/block>}}

---
title: Secure clusters
headerTitle: Secure clusters in YugabyteDB Managed
linkTitle: Secure clusters
description: Secure clusters in YugabyteDB Managed.
image: /images/section_icons/index/secure.png
headcontent: Configure the security features of your YugabyteDB Managed clusters
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-secure-clusters
weight: 30
type: indexpage
---

YugabyteDB Managed clusters include the following security features:

| Feature | Description |
| :--- | :--- |
| [Network authorization](add-connections/) | Access to YugabyteDB Managed clusters is limited to IP addresses that you explicitly allow using IP allow lists.<br>You can further enhance security and lower network latencies by deploying clusters in a [virtual private cloud (VPC) network](../cloud-basics/cloud-vpcs/). |
| [Database&nbsp;authorization](cloud-users/) | YugabyteDB uses [role-based access control](cloud-users/) for database authorization. Using the default database admin user that is created when a cluster is deployed, you can [add additional roles and users](add-users/) to provide custom access to database resources to other team members and database clients. |
| [Encryption in transit](cloud-authentication/) | YugabyteDB Managed uses encryption-in-transit for client-server and intra-node connectivity. |
| [Encryption at rest](managed-ear/) | Data at rest, including clusters and backups, is AES-256 encrypted using native cloud provider technologies: S3 and EBS volume encryption for AWS, Azure disk encryption, and server-side and persistent disk encryption for GCP. For additional security, you can encrypt your clusters using keys that you manage yourself. |
| [Auditing](cloud-activity/) | YugabyteDB Managed provides detailed auditing of activity on your account, including cluster creation, changes to clusters, changes to IP allow lists, backup activity, billing, access history, and more. |

{{<index/block>}}

  {{<index/item
    title="IP allow lists"
    body="Whitelist IP addresses to control who can connect to your clusters."
    href="add-connections/"
    icon="/images/section_icons/secure/tls-encryption/connect-to-cluster.png">}}

  {{<index/item
    title="Database authorization"
    body="Role-based access control in YugabyteDB Managed databases."
    href="cloud-users/"
    icon="/images/section_icons/secure/authorization.png">}}

  {{<index/item
    title="Add database users"
    body="Add users to your cluster databases."
    href="add-users/"
    icon="/images/section_icons/secure/create-roles.png">}}

  {{<index/item
    title="Encryption in transit"
    body="YugabyteDB Managed clusters use TLS and digital certificates to secure data in transit."
    href="cloud-authentication/"
    icon="/images/section_icons/secure/tls-encryption/connect-to-cluster.png">}}

  {{<index/item
    title="Encryption at rest"
    body="Use your own customer managed key to encrypt your clusters."
    href="managed-ear/"
    icon="/images/section_icons/secure/tls-encryption/connect-to-cluster.png">}}

  {{<index/item
    title="Audit account activity"
    body="Audit account activity, including changes to clusters, billing, allow lists, and more."
    href="cloud-activity/"
    icon="/images/section_icons/explore/monitoring.png">}}

{{</index/block>}}

---
title: Secure clusters
headerTitle: Secure clusters in YugabyteDB Aeon
linkTitle: Secure clusters
description: Secure clusters in YugabyteDB Aeon.
image: /images/section_icons/index/secure.png
headcontent: Configure the security features of your YugabyteDB Aeon clusters
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-secure-clusters
weight: 30
type: indexpage
---

YugabyteDB Aeon clusters include the following security features:

| Feature | Description |
| :--- | :--- |
| [Network authorization](add-connections/) | Access to YugabyteDB Aeon clusters is limited to IP addresses that you explicitly allow using IP allow lists.<br>You can further enhance security and lower network latencies by deploying clusters in a [virtual private cloud (VPC) network](../cloud-basics/cloud-vpcs/). |
| [Database&nbsp;authorization](cloud-users/) | YugabyteDB uses [role-based access control](cloud-users/) for database authorization. Using the default database admin user that is created when a cluster is deployed, you can [add additional roles and users](add-users/) to provide custom access to database resources to other team members and database clients. |
| [Encryption in transit](cloud-authentication/) | YugabyteDB Aeon uses encryption-in-transit for client-server and intra-node connectivity. |
| [Encryption at rest](managed-ear/) | Data at rest, including clusters and backups, is AES-256 encrypted using native cloud provider technologies: S3 and EBS volume encryption for AWS, Azure disk encryption, and server-side and persistent disk encryption for GCP. For additional security, you can encrypt your clusters using keys that you manage yourself. |
| [Auditing](cloud-activity/) | YugabyteDB Aeon provides detailed auditing of activity on your account, including cluster creation, changes to clusters, changes to IP allow lists, backup activity, billing, access history, and more. |

### Security profile

YugabyteDB Managed clusters all feature essential security features, such as encryption at rest, encryption in transit, RBAC, and auditing.

You can also create clusters using the **Advanced** security profile, which additionally enforces the following security features:

- The cluster must be deployed in a VPC.
- Public access can't be enabled; clusters can only be accessed from private addresses inside the VPC network.
- Scheduled backups are required. (Scheduled backups are turned on by default, but for clusters with the Advanced security profile, they can't be turned off.)

{{<index/block>}}

  {{<index/item
    title="IP allow lists"
    body="Whitelist IP addresses to control who can connect to your clusters."
    href="add-connections/"
    icon="/images/section_icons/secure/tls-encryption/connect-to-cluster.png">}}

  {{<index/item
    title="Database authorization"
    body="Role-based access control in YugabyteDB Aeon databases."
    href="cloud-users/"
    icon="/images/section_icons/secure/authorization.png">}}

  {{<index/item
    title="Add database users"
    body="Add users to your cluster databases."
    href="add-users/"
    icon="/images/section_icons/secure/create-roles.png">}}

  {{<index/item
    title="Encryption in transit"
    body="YugabyteDB Aeon clusters use TLS and digital certificates to secure data in transit."
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

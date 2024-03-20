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
| **Network authorization** | Access to YugabyteDB Managed clusters is limited to IP addresses that you explicitly allow using [IP allow lists](add-connections/).<br>You can further enhance security and lower network latencies by deploying clusters in a [virtual private cloud (VPC) network](../cloud-basics/cloud-vpcs/). |
| **Database authorization** | YugabyteDB uses [role-based access control](cloud-users/) for database authorization. Using the default database admin user that is created when a cluster is deployed, you can [add additional roles and users](add-users/) to provide custom access to database resources to other team members and database clients. |
| **Encryption in transit** | YugabyteDB Managed uses [encryption-in-transit](cloud-authentication/) for client-server and intra-node connectivity. |
| **Encryption at rest** | Data at rest, including clusters and backups, is AES-256 encrypted using native cloud provider technologies: S3 and EBS volume encryption for AWS, Azure disk encryption, and server-side and persistent disk encryption for GCP. For additional security, you can [encrypt your clusters](managed-ear/) using keys that you manage yourself. |
| **Auditing** | YugabyteDB Managed provides detailed [auditing of activity](cloud-activity/) on your account, including cluster creation, changes to clusters, changes to IP allow lists, backup activity, billing, access history, and more. |

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-connections/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/connect-to-cluster.png" aria-hidden="true" />
        <div class="title">IP allow lists</div>
      </div>
      <div class="body">
        Whitelist IP addresses to control who can connect to your clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-users/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authorization.png" aria-hidden="true" />
        <div class="title">Database authorization</div>
      </div>
      <div class="body">
        Role-based access control in YugabyteDB Managed databases.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-users/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/create-roles.png" aria-hidden="true" />
        <div class="title">Add database users</div>
      </div>
      <div class="body">
        Add users to your cluster databases.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/connect-to-cluster.png" aria-hidden="true" />
        <div class="title">Encryption in transit</div>
      </div>
      <div class="body">
        YugabyteDB Managed clusters use TLS and digital certificates to secure data in transit.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="managed-ear/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/connect-to-cluster.png" aria-hidden="true" />
        <div class="title">Encryption at rest</div>
      </div>
      <div class="body">
        Use your own customer managed key to encrypt your clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-activity/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Audit account activity</div>
      </div>
      <div class="body">
        Audit account activity, including changes to clusters, billing, allow lists, and more.
      </div>
    </a>
  </div>
</div>

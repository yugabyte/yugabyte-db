---
title: Secure clusters
headerTitle: Secure clusters in Yugabyte Cloud
linkTitle: Secure clusters
description: Secure clusters in Yugabyte Cloud.
image: /images/section_icons/index/secure.png
headcontent: Configure the security features of your Yugabyte Cloud clusters.
menu:
  latest:
    parent: yugabyte-cloud
    identifier: cloud-secure-clusters
weight: 30
---

Yugabyte Cloud clusters include the following security features:

Network authorization
: Access to Yugabyte Cloud clusters is limited to IP addresses that you explicitly allow using IP allow lists.
: You can further enhance security and lower network latencies by deploying clusters in a virtual private cloud (VPC) network.

Database authorization
: YugabyteDB uses role-based access control for database authorization. Using the default admin user that is created when a cluster is deployed, you can add additional roles and users to provide custom access to database resources to other team members and database clients.

Authentication
: Yugabyte Cloud uses encryption-in-transit for client-server and intra-node connectivity.

Auditing
: Yugabyte Cloud provides detailed tracking of activity on your cloud, including cluster creation, changes to clusters, changes to IP allow lists, backup activity, and billing.

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
    <a class="section-link icon-offset" href="cloud-vpcs/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">VPC networks</div>
      </div>
      <div class="body">
        VPC networking in Yugabyte Cloud.
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
        Role-based access control in Yugabyte Cloud databases.
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
    <a class="section-link icon-offset" href="cloud-activity/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Audit cloud activity</div>
      </div>
      <div class="body">
        Audit cloud activity, including changes to clusters, billing, allow lists, and more.
      </div>
    </a>
  </div>
</div>

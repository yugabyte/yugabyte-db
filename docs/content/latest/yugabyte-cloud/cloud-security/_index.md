---
title: Security
headerTitle: Security
linkTitle: Security
description: Security in Yugabyte Cloud.
image: /images/section_icons/index/secure.png
headcontent: Yugabyte Cloud infrastructure and security.
menu:
  latest:
    parent: yugabyte-cloud
    identifier: cloud-security
weight: 800
---

Yugabyte Cloud is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on public cloud providers such as Google Cloud Platform (GCP) and Amazon Web Services (AWS). Yugabyte Cloud uses a shared responsibility model, where security and compliance is a shared responsibility between public cloud providers, Yugabyte, and Yugabyte Cloud customers.

Security features of Yugabyte Cloud include:

- role-based access control for database authorization
- encryption-in-transit for client-server and intra-node connectivity
- encryption-at-rest
- limited network access using IP allow listing
- VPC peering

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-security-features/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/checklist.png" aria-hidden="true" />
        <div class="title">Security architecture</div>
      </div>
      <div class="body">
        Learn about Yugabyte Cloud's security architecture.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="shared-responsibility/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/grant-permissions.png" aria-hidden="true" />
        <div class="title">Shared responsibility model</div>
      </div>
      <div class="body">
        The Yugabyte Cloud shared responsibility model for security.
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

</div>

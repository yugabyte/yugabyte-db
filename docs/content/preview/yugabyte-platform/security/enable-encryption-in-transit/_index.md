---
title: Enable encryption in transit
headerTitle: Enable encryption in transit
linkTitle: Enable encryption in transit
description: Use YugabyteDB Anywhere to enable encryption in transit (TLS) on a YugabyteDB universe and connect to clients.
image: /images/section_icons/index/secure.png
headcontent: Use TLS certificates to secure inter- and intra-node communication
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: enable-encryption-in-transit
weight: 30
type: indexpage
---

YugabyteDB Anywhere allows you to protect data in transit by using the following:

- Server-to-server encryption for intra-node communication between YB-Master and YB-TServer nodes.
- Client-to-server encryption for communication between clients and nodes when using CLIs, tools, and APIs for YSQL and YCQL.
- Client-to-server encryption for communication between YugabytedDB Anywhere and other services, including OIDC, KMS, LDAP, Webhook, and backup storage.

{{< note title="Note" >}}

Client-to-server encryption in transit is not supported for YEDIS. Before you can enable client-to-server encryption, you first must enable server-to-server encryption.

{{< /note >}}

YugabyteDB Anywhere lets you create a new self-signed certificate, use an existing self-signed certificate, or upload a third-party certificate from external providers, such as Venafi or DigiCert (which is only available for an on-premises cloud provider).

You can enable encryption in transit (TLS) during universe creation and change these settings for an existing universe.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="encryption-in-transit-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption.png" aria-hidden="true" />
        <div class="title">Enable encryption in transit for universes</div>
      </div>
      <div class="body">
        Use encryption in transit for universe client-server and intra-node connections.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="encryption-in-transit-connect/">
      <div class="head">
       <img class="icon" src="/images/section_icons/secure/authentication.png" />
        <div class="title">Connect clients to universes</div>
      </div>
      <div class="body">
        Connect to universes using SSL/TLS.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="encryption-in-transit-services/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption.png" aria-hidden="true" />
        <div class="title">Add certificates to the trust store</div>
      </div>
      <div class="body">
        Add self-signed or private third-party Certificate Authority (CA) certificates to the YBA trust store.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="encryption-in-transit-validation/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption.png" aria-hidden="true" />
        <div class="title">Validation and troubleshooting</div>
      </div>
      <div class="body">
        Troubleshoot issues and validate certificates.
      </div>
    </a>
  </div>

</div>

---
title: TLS encryption
linkTitle: TLS encryption
description: TLS encryption
headcontent: Enable TLS encryption over the wire in YugabyteDB (Yugabyte Platform only).
image: /images/section_icons/secure/tls-encryption.png
block_indexing: true
menu:
  v1.3:
    identifier: secure-tls-encryption
    parent: secure
    weight: 720
---

YugabyteDB supports Transport Layer Security (TLS) encryption using [OpenSSL](https://www.openssl.org), which is natively available for Linux, BSD, and macOS operating systems. You can configure YugabyteDB to encrypt network communication, including:

* Server-server — between YB-Masters and YB-TServers
* Client-server — using CLIs and APIs for YSQL and YCQL

{{< note title="Note" >}}

YEDIS does not include support for client-server TLS encryption.

{{</note>}}

In this section, we will look at how to set up a 3-node YugabyteDB cluster with TLS encryption enabled.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="prepare-nodes/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/prepare-nodes.png" aria-hidden="true" />
        <div class="title">1. Prepare nodes</div>
      </div>
      <div class="body">
          Generate the per-node configuration and prepare the nodes with the configuration data.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="server-to-server/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/server-to-server.png" aria-hidden="true" />
        <div class="title">2. Server-server encryption</div>
      </div>
      <div class="body">
          Enable server-server encryption between YB-Masters and YB-TServers.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="client-to-server/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/client-to-server.png" aria-hidden="true" />
        <div class="title">3. Client-server encryption</div>
      </div>
      <div class="body">
          Enable client-server encryption for YSQL and YCQL.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-to-cluster/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/connect-to-cluster.png" aria-hidden="true" />
        <div class="title">4. Connect to cluster</div>
      </div>
      <div class="body">
          Connect to a YugabyteDB cluster with TLS encryption enabled.
      </div>
    </a>
  </div>
</div>

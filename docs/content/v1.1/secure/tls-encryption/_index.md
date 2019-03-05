---
title: TLS Encryption
linkTitle: TLS Encryption
description: TLS Encryption
headcontent: Enable TLS encryption over the wire in YugaByte DB (enterprise edition only).
image: /images/section_icons/secure/tls-encryption.png
aliases:
  - /secure/tls-encryption/
menu:
  v1.1:
    identifier: secure-tls-encryption
    parent: secure
    weight: 720
---

{{< note title="Note" >}}


TLS encryption is only supported in [YugaByte DB Enterprise Edition](https://www.yugabyte.com/enterprise-edition/).
{{< /note >}}

YugaByte DB uses OpenSSL (native to Linux/BSD operating systems) in order to perform TLS encryption. You can configure YugaByte DB to encrypt all network communication. The following communication is encrypted: 

* Server to server (for example, between YB-Masters and YB-TServers)
* Client to server (including connecting to the cluster using a command line shell)

Note that YEDIS does not currently support TLS encryption, however this is on the roadmap. Please [open a GitHub issue](https://github.com/YugaByte/yugabyte-db/issues) if this is of interest.

In this section, we will look at how to setup a 3 node YugaByte DB cluster with TLS encryption enabled.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="prepare-nodes/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/prepare-nodes.png" aria-hidden="true" />
        <div class="title">1. Prepare nodes</div>
      </div>
      <div class="body">
          Generate the per-node config and prepare the nodes with the config data.
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
          Enable server to server encryption between YB-Masters and YB-TServers.
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
          Enable client to server encryption.
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
          Connecting to a YugaByte DB cluster with TLS encryption enabled.
      </div>
    </a>
  </div>
</div>

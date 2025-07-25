---
title: Encryption in transit on YugabyteDB Clusters
headerTitle: Encryption in transit
linkTitle: Encryption in transit
description: Enable encryption in transit (using TLS) to protect network communication.
headcontent: Enable encryption in transit (using TLS) to protect network communication
menu:
  v2024.2:
    identifier: tls-encryption
    parent: secure
    weight: 725
type: indexpage
---

YugabyteDB can be configured to protect data in transit using the following:

- Server-to-server encryption for inter-node communication between YB-Master and YB-TServer nodes.
- Client-to-server encryption for communication between clients and nodes when using CLIs, tools, and APIs for YSQL and YCQL.

YugabyteDB supports [Transport Layer Security (TLS)](https://en.wikipedia.org/wiki/Transport_Layer_Security) encryption based on [OpenSSL](https://www.openssl.org) (v. 1.0.2u or later), an open source cryptography toolkit that provides an implementation of the Transport Layer Security (TLS) and Secure Socket Layer (SSL) protocols.

{{<index/block>}}

  {{<index/item
    title="Create server certificates"
    body="Create server certificates (using TLS) for protecting data in transit between YugabyteDB nodes."
    href="server-certificates/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Enable encryption in transit"
    body="Enable encryption (using TLS) between YB-Master and YB-TServer nodes."
    href="server-to-server/"
    icon="fa-thin fa-server">}}

  {{<index/item
    title="Connect to clusters"
    body="Connect clients, tools, and APIs to encryption-enabled YugabyteDB clusters."
    href="connect-to-cluster/"
    icon="fa-thin fa-plug">}}

  {{<index/item
    title="TLS and authentication"
    body="Use TLS encryption in conjunction with authentication."
    href="tls-authentication/"
    icon="fa-thin fa-user-lock">}}

{{</index/block>}}

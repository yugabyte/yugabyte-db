---
title: Encryption in transit in YugabyteDB Anywhere
headerTitle: Encryption in transit
linkTitle: Encryption in transit
description: Use encryption in transit (TLS) to secure data traffic.
headcontent: Secure intra-node and application traffic
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: enable-encryption-in-transit
    weight: 40
type: indexpage
showRightNav: true
---

YugabyteDB Anywhere allows you to protect data in transit by using the following:

- Node-to-Node TLS to encrypt intra-node communication between YB-Master and YB-TServer nodes.
- Client-to-Node TLS to encrypt communication between a universe and clients. This includes applications, shells (ysqlsh, ycqlsh, psql, and so on), and other tools, using the YSQL and YCQL APIs.

## Manage certificates

Use YugabyteDB Anywhere to manage certificates used for encryption in transit.

{{<index/block>}}

  {{<index/item
    title="Auto-generated certificates"
    body="YugabyteDB Anywhere can create and manage universe certificates."
    href="auto-certificate/"
    icon="fa-thin fa-certificate">}}

  {{<index/item
    title="Add certificates"
    body="Upload your own certificates to secure data transfer on your universes."
    href="add-certificate-self/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Rotate certificates"
    body="Update the certificates on universes when they expire."
    href="rotate-certificates/"
    icon="fa-thin fa-rotate">}}

  {{<index/item
    title="Trust Store"
    body="Add certificates to the YugabyteDB Anywhere Trust Store to validate connections from other services."
    href="trust-store/"
    icon="fa-thin fa-shop-lock">}}

{{</index/block>}}

## Enable encryption in transit

You enable Node-to-Node and Client-to-Node encryption in transit when you [create a universe](../../create-deployments/create-universe-multi-zone/).

You can also enable and disable encryption in transit for an existing universe as follows:

1. Navigate to your universe.
1. Click **Actions > Edit Security > Encryption in-Transit** to open the **Manage encryption in transit** dialog.
1. Enable or disable the **Enable encryption in transit for this Universe** option.
1. Click **Apply**.

### Enforce TLS versions

As TLS 1.0 and 1.1 are no longer accepted by PCI compliance, and considering significant vulnerabilities around these versions of the protocol, it is recommended that you migrate to TLS 1.2 or later versions.

You can set the TLS version for node-to-node and client-node communication. To enforce TLS 1.2, add the following flag for YB-TServer:

```shell
ssl_protocols = tls12
```

To enforce the minimum TLS version of 1.2, you need to specify all available subsequent versions for YB-TServer, as follows:

```shell
ssl_protocols = tls12,tls13
```

In addition, as the `ssl_protocols` setting does not propagate to PostgreSQL, it is recommended that you specify the minimum TLS version (`ssl_min_protocol_version`) for PostgreSQL by setting the following YB-TServer flag:

```shell
--ysql_pg_conf_csv="ssl_min_protocol_version='TLSv1.2'"
```

## Learn more

- [Securing YugabyteDB: Server-to-Server Encryption in Transit](https://www.yugabyte.com/blog/yugabytedb-server-to-server-encryption/)
- [Securing YugabyteDB: SQL Client-to-Server Encryption in Transit](https://www.yugabyte.com/blog/securing-yugabytedb-client-to-server-encryption/)
- [Securing YugabyteDB: CQL Client-to-Server Encryption in Transit](https://www.yugabyte.com/blog/securing-yugabytedb-part-3-cql-client-server-encryption-transit/)

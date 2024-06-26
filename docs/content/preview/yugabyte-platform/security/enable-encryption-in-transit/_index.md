---
title: Encryption in transit in YugabyteDB Anywhere
headerTitle: Encryption in transit
linkTitle: Encryption in transit
description: Use encryption in transit (TLS) to secure data traffic.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: enable-encryption-in-transit
    weight: 40
rightNav:
  hideH4: true
type: indexpage
---

YugabyteDB Anywhere allows you to protect data in transit by using the following:

- Node-to-Node TLS to encrypt intra-node communication between YB-Master and YB-TServer nodes.
- Client-to-Node TLS to encrypt communication between a universe and clients. This includes applications, shells (ysqlsh, ycqlsh, psql, and so on), and other tools, using the YSQL and YCQL APIs.
- Certificates added to the YugabyteDB Anywhere trust store to encrypt communication between YugabyteDB Anywhere and other services, including LDAP, OIDC, Hashicorp Vault, Webhook, and S3 backup storage.

YugabyteDB Anywhere can create and manage new self-signed certificates for encrypting data in transit. Alternatively, you can use your own self-signed certificates. You can also upload a third-party CA-signed certificate from external providers, such as Venafi or DigiCert. (CA-signed certificates can only be used with on-premises provider configurations.)

You can enable encryption in transit (TLS) during universe creation and change these settings for an existing universe.

{{<index/block>}}

  {{<index/item
    title="Auto-generated certificates"
    body="YugabyteDB Anywhere can create and manage universe certificates."
    href="auto-certificate/"
    icon="fa-thin fa-certificate">}}

  {{<index/item
    title="Add certificates"
    body="Upload your own certificates to secure data transfer on your universes."
    href="add-certificate-ca/"
    icon="fa-thin fa-file-certificate">}}

  {{<index/item
    title="Rotate certificates"
    body="Update the certificates on universes when they expire."
    href="rotate-certificates/"
    icon="fa-thin fa-rotate">}}

  {{<index/item
    title="Trust Store"
    body="Add certificates to the YugabyteDB Anywhere trust store for connecting from services such as LDAP, OIDC, Webhook, S3 backup storage, and Hashicorp Vault."
    href="trust-store/"
    icon="fa-thin fa-shop-lock">}}

{{</index/block>}}

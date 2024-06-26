---
title: Enable encryption in transit
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

Enabling encryption-in-transit requires the following steps:

1. If you are using a certificate that you provide, add your self- or CA-signed certificate to YugabyteDB Anywhere.
1. Enable encryption in transit on your universe. You can do this when creating the universe and on an existing universe.


### Expand a universe

You can expand universes configured with custom CA-signed certificates.

Before adding new nodes to expand an existing universe, you need to prepare those nodes by repeating Step 2 of [Use custom CA-signed certificates to enable TLS](#use-custom-ca-signed-certificates-to-enable-tls) for each of the new nodes you plan to add to the universe. You need to ensure that the certificates are signed by the same external CA and have the same root certificate. In addition, ensure that you copy the certificates to the same locations that you originally used when creating the universe.

When the universe is ready for expansion, complete the **Edit Universe** dialog to add new nodes.


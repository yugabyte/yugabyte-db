---
title: Use YugabyteDB Anywhere trust store
headerTitle: Add certificates to the trust store
linkTitle: Add certificates to the trust store
description: Add self-signed or custom certificates to the YugabyteDB Anywhere trust store.
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: eit-services
    weight: 40
type: docs
---

YugabyteDB Anywhere uses TLS to protect data in transit when connecting to other services, including:

- OIDC
- LDAP
- Webhook
- S3 backup storage
- Hashicorp Vault

If you are using self-signed or custom certificates, YugabyteDB cannot verify your TLS connections unless you add the certificates to the YugabyteDB Anywhere Trust Store.

## Add certificates to your trust store

To add a certificate to the YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click **Upload Trusted CA Certificate**.

1. Enter a name for the certificate.

1. Click **Upload**, select your certifcate (in .crt format) and click **Save CA Certificate**.

## Rotate a certificate in your trust store

To rotate a certificate in your YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click thew **...** button for the certificate and choose **Update Certificate**.

1. Enter a name for the certificate.

1. Click **Upload**, select your certifcate (in .crt format) and click **Save CA Certificate**.

## Delete a certificate in your trust store

To delete a certificate in your YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click thew **...** button for the certificate and choose **Delete**, then click **Delete CA Certificate**.

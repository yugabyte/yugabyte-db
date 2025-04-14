---
title: Add CA-signed certificates to YugabyteDB Anywhere
headerTitle: Add certificates to your trust store
linkTitle: Trust store
description: Add certificates to the YugabyteDB Anywhere trust store.
headcontent: Add certificates for third-party services
menu:
  v2024.1_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: trust-store
    weight: 40
type: docs
---

YugabyteDB Anywhere uses certificates to validate connections between YugabyteDB Anywhere and other external services, including:

- [LDAP](../../../administer-yugabyte-platform/ldap-authentication/)
- [OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [Webhook](../../../alerts-monitoring/set-up-alerts-health-check/)
- [S3 backup storage](../../../back-up-restore-universes/configure-backup-storage/)
- [Hashicorp Vault](../../create-kms-config/hashicorp-kms/)
- Other [YugabyteDB Anywhere high availability](../../../administer-yugabyte-platform/high-availability/) replicas.

When using self-signed or custom CA certificates, to enable YugabyteDB Anywhere to validate your TLS connections, you _must_ add the certificates to the YugabyteDB Anywhere Trust Store

## Add certificates to your trust store

To add a certificate to the YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click **Upload Trusted CA Certificate**.

1. Enter a name for the certificate.

1. Click **Upload**, select your certificate (in .crt format) and click **Save CA Certificate**.

## Rotate a certificate in your trust store

To rotate a certificate in your YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click the **...** button for the certificate and choose **Update Certificate**.

1. Click **Upload**, select your certificate (in .crt format) and click **Save CA Certificate**.

## Delete a certificate in your trust store

To delete a certificate in your YugabyteDB Anywhere Trust Store, do the following:

1. Navigate to **Admin > CA Certificates**.

1. Click the **...** button for the certificate and choose **Delete**, then click **Delete CA Certificate**.

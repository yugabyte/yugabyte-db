---
title: Rotate certificates on YugabyteDB Anywhere
headerTitle: Rotate certificates
linkTitle: Rotate certificates
description: Rotate certificates on YugabyteDB Anywhere.
headcontent: Rotate certificates used by a universe
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: rotate-certificates
    weight: 30
type: docs
---

You can rotate certificates for universes configured with the same type of certificates. This involves replacing existing certificates with new database node certificates.

Before rotating certificates, ensure that you have added the certificates to YugabyteDB Anywhere. Refer to [Add certificates](../add-certificate-self/).

## Rotating certificates

Rotating certificates may require a restart of the YB-Master and YB-TServer processes and in some circumstances can result in downtime.

- Client-to-node certificates

  Regardless of whether the client-to-node certificates are expired or not expired, you can always trigger a rolling upgrade to rotate the certificates.

  - If the universe was created before v2.16.6, then the rotation requires a restart, which can be done in a rolling manner with no downtime.
  - If the universe was created after v2.16.6, then the rotation can be done without a restart and no downtime.

- Node-to-node certificates

  If the certificate has expired, the rotation requires a simultaneous restart of all nodes, resulting in some downtime.

  If the certificate has not expired, the rotation can be done using a rolling upgrade.

  - If the universe was created before v2.16.6, then the rotation requires a restart, which can be done in a rolling manner with no downtime.
  - If the universe is created after v2.16.6, then the rotation can be done without a restart and no downtime.

You can always opt to not perform rolling updates to update all nodes at the same time, but this will result in downtime.

### Rotate certificates

To modify encryption in transit settings and rotate certificates for a universe, do the following:

1. Navigate to your universe.

1. Click **Actions > Edit Security > Encryption in-Transit** to open the **Manage encryption in transit** dialog.

    ![Rotate certificates](/images/yp/encryption-in-transit/rotate-cert.png)

1. Enable or disable encryption in transit.

1. To rotate the root certificate, on the **Certificate Authority** tab, select the new root certificate(s).

    Delete the root certificate to create a new [self-signed certificate](../auto-certificate/).

1. To rotate the server certificates, on the **Server Certificate** tab, select the **Rotate Node-to-Node Server Certificate** and **Rotate Client-to-Node Server Certificate** options as appropriate.

1. Select the **Use rolling upgrade to apply this change** option to perform the upgrade in a rolling update (recommended) and enter the number of seconds to wait between node upgrades.

1. Click **Apply**.

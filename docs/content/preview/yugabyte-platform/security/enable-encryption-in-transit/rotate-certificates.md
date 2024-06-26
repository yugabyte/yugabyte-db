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

Rotating certificates requires restart of the YB-Master and YB-TServer processes and can result in downtime. To avoid downtime, you can opt to perform a rolling upgrade, which stops, updates, and restarts each node in the universe with a specific delay between node upgrades (as opposed to a simultaneous change of certificates in every node is updated at the same time).

## Rotate certificates

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

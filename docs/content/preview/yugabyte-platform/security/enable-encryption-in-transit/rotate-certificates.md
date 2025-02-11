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

Before rotating certificates, ensure that you have added the new certificates to YugabyteDB Anywhere. Refer to [Add certificates](../add-certificate-self/).

Rotating the CA certificate on the source universe with xCluster Replication causes replication to pause. You should [restart replication](../../../manage-deployments/xcluster-replication/xcluster-replication-setup/#restart-replication) after completing the CA certificate rotation on the source universe.

## Enable or disable encryption in transit

You can enable or disable:

- encryption in transit for the universe
- node-to-node encryption in transit
- client-to-node encryption in transit

This requires a simultaneous restart of all nodes, resulting in some downtime.

To enable or disable encryption in transit:

1. Navigate to your universe.

1. Click **Actions > More > Edit Security > Encryption in-Transit** to open the **Manage encryption in transit** dialog.

    ![Enable encryption in transit](/images/yp/encryption-in-transit/enable-eit.png)

1. Set the **Enable encryption in transit for this Universe** option.

1. On the **Certificate Authority** tab, set the **Enable Node to Node Encryption** and **Enable Client to Node Encryption** options.

1. If you are enabling node-to-node or client-to-node encryption, select the root certificate to use, or leave the **Select root certificate** field empty to have YugabyteDB Anywhere generate a self-signed certificate.

1. Click **Apply**.

YugabyteDB Anywhere restarts the universe.

## Rotate certificates

**Node-to-node certificates**

If your node-to-node root certificate has expired, rotation requires a simultaneous restart of all nodes, resulting in some downtime.

If the certificate has not expired:

- If the universe was created using YugabyteDB Anywhere v2.16.5 and earlier, then the rotation requires a restart, which can be done in a rolling manner with no downtime. You can opt to not perform a rolling update to update all nodes at the same time, but this will result in downtime.
- If the universe was created using YugabyteDB Anywhere v2.16.6 or later, then the rotation is done without a restart and no downtime.

**Client-to-node certificates**

If the universe was created using YugabyteDB Anywhere v2.16.5 and earlier, then the rotation requires a restart. This can be done in a rolling manner with no downtime, regardless of whether the client-to-node certificates are expired or not expired.

If the universe was created using YugabyteDB Anywhere v2.16.6 or later, then the rotation is done without a restart and no downtime.

If you change your client-to-node root certificate, be sure to update your clients and applications to use the new certificate. Refer to [Download the universe certificate](../../../create-deployments/connect-to-universe/#download-the-universe-certificate).

### Rotate server certificates

To rotate server (node) certificates for a universe, do the following:

1. Navigate to your universe.

1. Click **Actions > More > Edit Security > Encryption in-Transit** to open the **Manage encryption in transit** dialog.

1. On the **Server Certificate** tab, select the **Rotate Node-to-Node Server Certificate** and **Rotate Client-to-Node Server Certificate** options as appropriate.

1. If the universe was created using YugabyteDB Anywhere v2.16.5 and earlier, select the **Use rolling upgrade to apply this change** option to perform the upgrade in a rolling update (recommended) and enter the number of seconds to wait between node upgrades. Otherwise, deselect this option to perform a hot certificate reload with no downtime.

1. Click **Apply**.

### Rotate root certificates

To rotate root certificates for a universe, do the following:

1. Navigate to your universe.

1. Click **Actions > More > Edit Security > Encryption in-Transit** to open the **Manage encryption in transit** dialog.

1. On the **Certificate Authority** tab, select the new root certificate(s).

    If your certificate is not listed, ensure you have [added the certificate](../add-certificate-ca/) to YugabyteDB Anywhere.

    To have YBA generate a new self-signed CA certificate [automatically](../auto-certificate/), clear the root certificate field.

    Note that when you rotate the root certificate, the server certificates are automatically rotated.

1. If the universe was created using YugabyteDB Anywhere v2.16.5 and earlier, select the **Use rolling upgrade to apply this change** option to perform the upgrade in a rolling update (recommended) and enter the number of seconds to wait between node upgrades. Otherwise, deselect this option to perform a hot certificate reload with no downtime.

1. Click **Apply**.

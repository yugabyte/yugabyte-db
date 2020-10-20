---
title: Configure KMS using Equinix SmartKey
headerTitle: Configure KMS using Equinix SmartKey
linkTitle: Configure KMS
description: Use Yugabyte Platform to configure KMS using Equinix SmartKey.
menu:
  latest:
    parent: secure-universes
    identifier: configure-kms-2-smartkey-kms
    weight: 20
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./aws-kms" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>

  <li >
    <a href="{{< relref "./equinix-smartkey" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Equinix SmartKey
    </a>
  </li>

</ul>

Encryption at rest in Yugabyte Platform supports the use of [Equinix SmartKey](https://www.equinix.com/services/edge-services/smartkey/). Equinix SmartKey is a global SaaS-based, secure key management and cryptography service offered on the cloud-neutral [Platform Equinix](https://www.equinix.com/platform-equinix/).

To configure KMS with Equinix SmartKey for use with encryption at rest, follow these steps:

1. Open the Yugabyte Platform console and click **Configs**.
2. Click the **Security** tab and then click the **Encryption At Rest** tab. A list of configurations appears.
3. Click **Create New Config**. A new configuration form appears.
4. Enter the following configuration details:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select `Equinix SmartKey`.
    - **API Url** – Enter the URL for the API. The default is `api.amer.smartkey.io`.
    - **Secret API Key** — Enter the secret API key.

5. Click **Save**. Your new configuration should appear in the list of configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.

6. (Optional) To confirm the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

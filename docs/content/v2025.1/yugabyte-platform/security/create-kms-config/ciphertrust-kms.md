---
title: Create a KMS configuration using CipherTrust
headerTitle: Create a KMS configuration
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for CipherTrust KMS.
menu:
  v2025.1_yugabyte-platform:
    parent: security
    identifier: create-kms-config-5-ciphertrust-kms
    weight: 50
type: docs
---

Encryption at rest uses a master key to encrypt and decrypt universe keys. The master key details are stored in YugabyteDB Anywhere in key management service (KMS) configurations. You enable encryption at rest for a universe by assigning the universe a KMS configuration. The master key designated in the configuration is then used for generating the universe keys used for encrypting the universe data.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../aws-kms/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>
  <li >
    <a href="../google-kms/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      Google
    </a>
  </li>

  <li >
    <a href="../azure-kms/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li >
    <a href="../hashicorp-kms/" class="nav-link">
      HashiCorp
    </a>
  </li>

  <li >
    <a href="../ciphertrust-kms/" class="nav-link active">
      CipherTrust
    </a>
  </li>

</ul>

Encryption at rest in YugabyteDB Anywhere supports the use of [CipherTrust KMS](https://thalesdocs.com/ctp/cm/latest/).

## Prerequisites

{{<tags/feature/ea idea="1227">}}CipherTrust support is Early Access. To enable the feature in YugabyteDB Anywhere, set the **Allow CipherTrust KMS** Global Runtime Configuration option (config key `yb.kms.allow_ciphertrust`) to true. Refer to [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

## Create a KMS configuration

You can create a KMS configuration that uses CipherTrust, as follows:

1. Navigate to **Integrations > Security > Encryption At Rest** to access the list of existing configurations.

1. Click **Create New Config**.

1. Enter the following configuration details in the form:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **CipherTrust KMS**.
    - **CipherTrust Manager URL** — Enter the URL of your CipherTrust Manager deployment.
    - **Authentication type** — Choose **User Credentials** to provide a username and password, or **Refresh Token** to provide a token.
    - **Key Name** — Enter the name of the key. If a key with the same name already exists, the existing key is used; otherwise, a new key is created automatically using the specified algorithm and size.
    - **Key Algorithm** — Choose the encryption algorithm to use to create a new key.
    - **Key Size** — Choose the key size for a new key.

    ![Ciphertrust KMS](/images/yp/security/kms-ciphertrust-config.png)

1. Click **Save**.

    Your new configuration should appear in the list of configurations.

1. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

## Modify a KMS configuration

You can modify the credentials to use to access your CipherTrust Manager as follows:

1. Navigate to **Integrations > Security > Encryption At Rest** to open a list of existing configurations.

1. Find the configuration you want to modify and click its corresponding **Actions > Edit Configuration**.

1. Provide new values for the **Authentication** fields.

1. Click **Save**.

1. Optionally, to confirm that the information is correct, click **Show details** or **Actions > Details**.

## Delete a KMS configuration

{{<note title="Note">}}
Without a KMS configuration, you would longer be able to decrypt universe keys that were encrypted using the master key in the KMS configuration. Even after a key is rotated out of service, it may still be needed to decrypt data in backups and snapshots that were created while it was active. For this reason, you can only delete a KMS configuration if it has never been used by any universes.
{{</note>}}

To delete a KMS configuration, click its corresponding **Actions > Delete Configuration**.

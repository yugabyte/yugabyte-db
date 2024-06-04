---
title: Create a KMS configuration using Azure
headerTitle: Create a KMS configuration
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for Azure Key Vault.
menu:
  v2.20_yugabyte-platform:
    parent: security
    identifier: create-kms-config-3-azure-kms
    weight: 50
type: docs
---

Encryption at rest uses a master key to encrypt and decrypt universe keys. The master key details are stored in YugabyteDB Anywhere in key management service (KMS) configurations. You enable encryption at rest for a universe by assigning the universe a KMS configuration. The master key designated in the configuration is then used for generating the universe keys used for encrypting the universe data.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../aws-kms/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>
  <li >
    <a href="../google-kms/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      Google KMS
    </a>
  </li>
  <li >
    <a href="../azure-kms/" class="nav-link active">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure Key Vault
    </a>
  </li>
  <li >
    <a href="../hashicorp-kms/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>
</ul>

Encryption at rest in YugabyteDB Anywhere supports the use of Microsoft [Azure Key Vault](https://learn.microsoft.com/en-us/azure/key-vault/).

Conceptually, Azure Key Vault consists of a key vault containing one or more keys, with each key capable of having multiple versions.

## Prerequisites

Before defining a KMS configuration with YugabyteDB Anywhere, you need to create a key vault through the [Azure portal](https://docs.microsoft.com/en-us/azure/key-vault/general/quick-create-portal). The following settings are required:

- Set the vault permission model as Vault access policy.
- Add the application to the key vault access policies with the minimum key management operations permissions of Get and Create (unless you are pre-creating the key), as well as cryptographic operations permissions of Unwrap Key and Wrap Key.

If you are planning to use an existing cryptographic key with the same name, it must meet the following criteria:

- The primary key version should be in the Enabled state.
- The activation date should either be disabled or set to a date before the KMS configuration creation.
- The expiration date should be disabled.
- Permitted operations should have at least WRAP_KEY and UNWRAP_KEY.
- The key rotation policy should not be defined in order to avoid automatic rotation.

Note that YugabyteDB Anywhere does not manage the key vault and deleting the KMS configuration does not delete the key vault, master key, or key versions on Azure Key Vault.

## Create a KMS configuration

You can create a KMS configuration that uses Azure Key Vault, as follows:

1. Use the YugabyteDB Anywhere UI to navigate to **Configs > Security > Encryption At Rest** to access the list of existing configurations.

1. Click **Create New Config**.

1. Enter the following configuration details in the form:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **Azure KMS**.
    - **Client ID** — Enter the Azure Active Directory (AD) application client ID.
    - **Client Secret** — Enter the Azure AD application client secret.
    - **Tenant ID** — Enter the Azure AD application tenant ID.
    - **Key Vault URL** — Enter the key vault URI, as per your Azure portal Key Vault definition that should allow you to use the preceding three credentials to gain access to an application created in the Azure AD.
    - **Key Name** — Enter the name of the master key. If a master key with the same name already exists in the key vault, the settings are validated and the existing key is used; otherwise, a new key is created automatically.
    - **Key Algorithm** — The algorithm for the master key. Currently, only the RSA algorithm is supported.
    - **Key Size** — Select the size of the master key, in bits. Supported values are 2048 (default), 3072, and 4096.

    ![Azure Key Vault configuration](/images/yp/security/azurekms-config.png)

1. Click **Save**.

    Your new configuration should appear in the list of configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.

1. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

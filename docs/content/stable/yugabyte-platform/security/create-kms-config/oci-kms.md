---
title: Create a KMS configuration using OCI Vault
headerTitle: Create a KMS configuration
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for Oracle Cloud Infrastructure (OCI) Vault.
menu:
  stable_yugabyte-platform:
    parent: security
    identifier: create-kms-config-4-oci-kms
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
    <a href="../oci-kms/" class="nav-link active">
      <i class="icon-oracle" aria-hidden="true"></i>
      OCI
    </a>
  </li>

  <li >
    <a href="../hashicorp-kms/" class="nav-link">
      HashiCorp
    </a>
  </li>

  <li >
    <a href="../ciphertrust-kms/" class="nav-link">
      CipherTrust
    </a>
  </li>

</ul>

Encryption at rest in YugabyteDB Anywhere supports the use of [OCI Vault](https://docs.oracle.com/en-us/iaas/Content/KeyManagement/Concepts/keyoverview.htm).

If you are planning to use an existing cryptographic key with the same name, it must meet the following criteria:

- The key should be in the Enabled state.
- The purpose should be set to symmetric encryption (AES). YugabyteDB Anywhere uses AES-256.

Note that YugabyteDB Anywhere does not manage the vault. Deleting the KMS configuration does not delete the vault, master key, or key versions on OCI Vault.

## Prerequisites

The OCI user or instance principal associated with a KMS configuration requires permissions to manage keys in the vault. See [To use encryption at rest with YugabyteDB Anywhere](../../../prepare/cloud-permissions/cloud-permissions-ear/).

Create the vault in the OCI Console before you create the KMS configuration. YugabyteDB Anywhere uses the vault you specify; it creates a key in that vault only if a key with the given display name does not already exist.

## Create a KMS configuration

You can create a KMS configuration that uses OCI Vault, as follows:

1. Navigate to **Integrations > Security > Encryption At Rest** to access the list of existing configurations.

1. Click **Create New Config**.

1. Enter the following configuration details in the form:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **OCI KMS**.
    - **Authentication Type** — Choose **API Key** to supply OCI API signing key credentials, or **Instance Principal** to use the YBA host's instance identity without storing credentials.
    - **User OCID**, **Tenancy OCID**, **Fingerprint**, and **Private Key** — Required when using API Key authentication. Upload the PEM private key file.
    - **Region** — Select the OCI region where the vault and key are located. This setting does not need to match the region where the encrypted universe resides.
    - **Compartment OCID** — OCID of the compartment where the key will be created (or already exists).
    - **Vault OCID** — OCID of the vault that will contain the encryption key.
    - **Key Name** — Display name of the OCI Vault key. If a key with this name exists, YugabyteDB Anywhere uses it; otherwise it creates one.

1. Click **Save**.

    Your new configuration should appear in the list of configurations.

1. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

## Modify a KMS configuration

You can modify an existing KMS configuration as follows:

1. Navigate to **Integrations > Security > Encryption At Rest** to open a list of existing configurations.

1. Find the configuration you want to modify and click its corresponding **Actions > Edit Configuration**.

1. You can update the API signing key credentials (User OCID, Tenancy OCID, Fingerprint, and Private Key) and the Compartment OCID. Authentication type, region, vault OCID, and key name cannot be changed.

1. Click **Save**.

1. Optionally, to confirm that the information is correct, click **Show details** or **Actions > Details**.

## Delete a KMS configuration

{{<note title="Note">}}
Without a KMS configuration, you would no longer be able to decrypt universe keys that were encrypted using the master key in the KMS configuration. Even after a key is rotated out of service, it may still be needed to decrypt data in backups and snapshots that were created while it was active. For this reason, you can only delete a KMS configuration if it has never been used by any universes.
{{</note>}}

To delete a KMS configuration, click its corresponding **Actions > Delete Configuration**.

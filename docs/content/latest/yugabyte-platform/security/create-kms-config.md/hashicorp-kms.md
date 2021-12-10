---
title: Create a KMS configuration using HashiCorp Vault
headerTitle: Create a KMS configuration using HashiCorp Vault
linkTitle: Create a KMS configuration
description: Use Yugabyte Platform to create a KMS configuration for HashiCorp Vault.
aliases:
  - /latest/yugabyte-platform/security/create-kms-config
menu:
  latest:
    parent: security
    identifier: create-kms-config-3-hashicorp-kms
    weight: 27
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./aws-kms.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>

  <li >
    <a href="{{< relref "./equinix-smartkey.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Equinix SmartKey
    </a>
  </li>

  <li >
    <a href="{{< relref "./hashicorp-kms.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>

</ul>

Encryption at rest uses universe keys to encrypt and decrypt universe data keys. You can use the Yugabyte Platform UI to create key management service (KMS) configurations for generating the required universe keys for one or more YugabyteDB universes. Encryption at rest in Yugabyte Platform supports the use of [HashiCorp Vault](https://www.vaultproject.io/) as a KMS.

You can create a KMS configuration that uses HashiCorp Vault as follows:

1. Open the Yugabyte Platform console and navigate to **Configs > Security > Encryption At Rest**. A list of existing configurations appears.

2. Click **Create New Config**.

3. Provide the following configuration details:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **Hashicorp**.
    - **Vault Address** — Enter the web address of your vault. For example, `http://127.0.0.1:8200`
    - **Secret Token** — Enter the token you obtained from the vault.
    - **Secret Engine** — This is a read-only field with its value set to `transit`. It identifies the secret engine.
    - **Mount Path** — Specify the path to the secret engine within the vault. The default value is `transit/`.
    
4. Click **Save**. Your new configuration should appear in the list of configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.

6. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.
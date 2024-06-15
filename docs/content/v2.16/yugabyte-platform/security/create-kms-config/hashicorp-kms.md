---
title: Create a KMS configuration using HashiCorp Vault
headerTitle: Create a KMS configuration using HashiCorp Vault
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for HashiCorp Vault.
menu:
  v2.16_yugabyte-platform:
    parent: security
    identifier: create-kms-config-3-hashicorp-kms
    weight: 27
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./aws-kms.md" >}}" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>
  <li >
    <a href="{{< relref "./google-kms.md" >}}" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      Google KMS
    </a>
  </li>
  <li >
    <a href="{{< relref "./azure-kms.md" >}}" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure Key Vault
    </a>
  </li>
  <li >
    <a href="{{< relref "./hashicorp-kms.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>
</ul>

Encryption at rest uses universe keys to encrypt and decrypt universe data keys. You can use the YugabyteDB Anywhere UI to create key management service (KMS) configurations for generating the required universe keys for one or more YugabyteDB universes. Encryption at rest in YugabyteDB Anywhere supports the use of [HashiCorp Vault](https://www.vaultproject.io/) as a KMS.

## Configure HashiCorp Vault

Before you can start configuring HashiCorp Vault, install it on a virtual machine, as per instructions provided in [Install Vault](https://www.vaultproject.io/docs/install). The vault can be set up as a multi-node cluster. Ensure that your vault installation meets the following requirements:

- Has transit secret engine enabled.
- Its seal and unseal mechanism is secure and repeatable.
- Its token creation mechanism is repeatable.

You need to configure HashiCorp Vault in order to use it with YugabyteDB Anywhere, as follows:

1. Create a vault configuration file that references your nodes and specifies the address, as follows:

    ```properties
    storage "raft" {
    path  = "./vault/data/"
    node_id = "node1"
    }

    listener "tcp" {
    address   = "127.0.0.1:8200"
    tls_disable = "true"
    }

    api_addr = "http://127.0.0.1:8200"
    cluster_addr = "https://127.0.0.1:8201"
    ui = true
    disable_mlock = true
    default_lease_ttl = "768h"
    max_lease_ttl = "8760h"
    ```

    Replace `127.0.0.1` with the vault web address.

    For additional configuration options, see [Parameters](https://www.vaultproject.io/docs/configuration#parameters).

1. Initialize the vault server by following instructions provided in [Operator init](https://www.vaultproject.io/docs/commands/operator/init).

1. Allow access to the vault by following instructions provided in [Unsealing](https://www.vaultproject.io/docs/concepts/seal#unsealing).

1. Enable the secret engine by executing the following command:

    ```shell
    vault secrets enable transit
    ```

    For more information, see [Transit Secrets Engine](https://www.vaultproject.io/docs/secrets/transit) and [Setup](https://www.vaultproject.io/docs/secrets/transit#setup).

1. Create the vault policy, as per the following sample:

    ```properties
    path "transit/*" {
      capabilities = ["create", "read", "update", "delete", "list"]
    }

    path "auth/token/lookup-self" {
            capabilities = ["read"]
    }

    path "sys/capabilities-self" {
            capabilities = ["read", "update"]
    }

    path "auth/token/renew-self" {
            capabilities = ["update"]
    }

    path "sys/*" {
            capabilities = ["read"]
    }
    ```

1. Generate a token with appropriate permissions (as per the referenced policy) by executing the following command:

    ```shell
    vault token create -no-default-policy -policy=trx
    ```

    You may also specify the following for your token:

    - `ttl` — Time to live (TTL). If not specified, the default TTL of 32 days is used, which means that the generated token will expire after 32 days.

    - `period` — If specified, the token can be infinitely renewed.

    YBA automatically tries to renew the token every 12 hours after it has passed 70% of its expiry window; as a result, you should set the TTL or period to be greater than 12 hours.

    For more information, refer to [Tokens](https://developer.hashicorp.com/vault/tutorials/tokens/tokens) in the Hashicorp documentation.

## Create a KMS configuration

You can create a new KMS configuration that uses HashiCorp Vault as follows:

1. Using the YugabyteDB Anywhere UI, navigate to **Configs > Security > Encryption At Rest** to open a list of existing configurations.

1. Click **Create New Config**.

1. Provide the following configuration details:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **Hashicorp Vault**.
    - **Vault Address** — Enter the web address of your vault. For example, `http://127.0.0.1:8200`
    - **Secret Token** — Enter the token you obtained from the vault.
    - **Secret Engine** — This is a read-only field with its value set to `transit`. It identifies the secret engine.
    - **Mount Path** — Specify the path to the secret engine in the vault. The default value is `transit/`.

    ![Hashicorp KMS configuration](/images/yp/security/hashicorp-config.png)

1. Click **Save**. Your new configuration should appear in the list of configurations.

1. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

## Modify a KMS configuration

You can modify an existing KMS configuration as follows:

1. Using the YugabyteDB Anywhere UI, navigate to **Configs > Security > Encryption At Rest** to open a list of existing configurations.

1. Find the configuration you want to modify and click its corresponding **Actions > Edit Configuration**.

1. Provide new values for the **Vault Address** and **Secret Token** fields.

1. Click **Save**.

1. Optionally, to confirm that the information is correct, click **Show details** or **Actions > Details**.

## Delete a KMS configuration

To delete a KMS configuration, click its corresponding **Actions > Delete Configuration**.

Note that a saved KMS configuration can only be deleted if it is not in use by any existing universes.

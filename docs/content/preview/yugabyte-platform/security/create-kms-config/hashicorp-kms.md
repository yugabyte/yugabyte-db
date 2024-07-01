---
title: Create a KMS configuration using HashiCorp Vault
headerTitle: Create a KMS configuration
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for HashiCorp Vault.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: create-kms-config-4-hashicorp-kms
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
    <a href="../azure-kms/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure Key Vault
    </a>
  </li>
  <li >
    <a href="../hashicorp-kms/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>
</ul>

Encryption at rest in YugabyteDB Anywhere supports the use of [HashiCorp Vault](https://www.vaultproject.io/) as a KMS.

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

1. If you want to use a token for authentication, generate a token with appropriate permissions (as per the referenced policy) by executing the following command:

    ```shell
    vault token create -no-default-policy -policy=trx
    ```

    You may also specify the following for your token:

    - `ttl` — Time to live (TTL). If not specified, the default TTL of 32 days is used, which means that the generated token will expire after 32 days.

    - `period` — If specified, the token can be infinitely renewed.

    YugabyteDB Anywhere automatically tries to renew the token every 12 hours after it has passed 70% of its expiry window; as a result, you should set the TTL or period to be greater than 12 hours.

    For more information, refer to [Tokens](https://developer.hashicorp.com/vault/tutorials/tokens/tokens) in the Hashicorp documentation.

1. If you want to use AppRole for authentication, do the following:

    - Obtain AppRole credentials by enabling the auth method in the vault using the following command:

        ```sh
        vault auth enable approle
        ```

        Refer to [Enable AppRole auth method](https://developer.hashicorp.com/vault/tutorials/cloud/vault-auth-method#enable-approle-auth-method).

    - Generate a role with appropriate permissions (as per the referenced policy) by executing the following command:

        ```sh
        vault write auth/approle/role/ybahcv token_policies="trx"
        ```

        You may also specify the following for your token generated from the AppRole:

        - `ttl` — Time to live (TTL). If not specified, the default TTL of 32 days is used, which means that the generated token will expire after 32 days.
        - `period` — If specified, the token can be infinitely renewed.

    - Obtain the RoleID and SecretID using the following commands:

        ```sh
        vault read auth/approle/role/ybahcv/role-id
        vault write -force auth/approle/role/ybahcv/secret-id
        ```

        Refer to [Generate RoleID and SecretID](https://developer.hashicorp.com/vault/tutorials/cloud/vault-auth-method#generate-roleid-and-secretid).

## Create a KMS configuration

You can create a new KMS configuration that uses HashiCorp Vault as follows:

1. Using the YugabyteDB Anywhere UI, navigate to **Configs > Security > Encryption At Rest** to open a list of existing configurations.

1. Click **Create New Config**.

1. Provide the following configuration details:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **Hashicorp Vault**.
    - **Vault Address** — Enter the web address of your vault. For example, `http://127.0.0.1:8200`
    - **Authentication Type** — Choose the authentication method to use, Token or AppRole.

        For Token, enter the token you obtained from the vault.

        For AppRole, enter the role ID and corresponding secret ID obtained from the vault. If the vault is configured with namespaces, also enter the namespace used to generate the credentials.

    - **Key Name** - Enter the name of the Master Key in the vault. If you don't provide a key name, the configuration uses the default name key_yugabyte. If a key with that name exists in the vault, the configuration will use it, otherwise a key with that name is created.
    - **Secret Engine** — This is a read-only field with its value set to `transit`. It identifies the secret engine.
    - **Mount Path** — Specify the path to the secret engine in the vault. The default value is `transit/`.

    ![Create config](/images/yp/security/hashicorp-config.png)

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

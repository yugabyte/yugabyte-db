---
title: Hashicorp Vault
linkTitle: Hashicorp Vault
description: Use YSQL plugin for Hashicorp Vault
aliases:
menu:
  preview_integrations:
    identifier: hashicorp-vault
    parent: integrations
    weight: 571
type: docs
---

HashiCorp Vault is designed to help organizations manage access to secrets and transmit them safely in an organization.

Secrets are defined as any form of sensitive credentials that need to be tightly controlled and monitored and can be used to unlock sensitive information. Secrets could be in the form of passwords, API keys, SSH keys, RSA tokens, or OTP.

For more details, refer to [Dynamic secrets](https://github.com/yugabyte/hashicorp-vault-ysql-plugin#dynamic-secrets).

## YSQL plugin for Hashicorp Vault

YSQL plugin provides APIs for using the HashiCorp Vault's dynamic secrets for YugabyteDB. The plugin includes APIs which can be used to add YugabyteDB to manage secrets, create new users, and manage [leases](https://developer.hashicorp.com/vault/docs/concepts/lease).

For more details, refer to [YSQL plugin for Hashicorp Vault](https://github.com/yugabyte/hashicorp-vault-ysql-plugin#ysql-plugin-for-hashicorp-vault-1).

## Setup

Before using the vault, do the following:

1. Add go to the path as follows:

    ```sh
    export GOPATH=$HOME/go
    export PATH=$PATH:$GOROOT/bin:$GOPATH/bin
    ```

1. Clone the `hashicorp-vault-ysql-plugin` repository:

    ```sh
    git clone https://github.com/yugabyte/hashicorp-vault-ysql-plugin && cd hashicorp-vault-ysql-plugin
    ```

1. Build the plugin as follows:

    ```go
    go build -o <build dir>/ysql-plugin cmd/ysql-plugin/main.go
    ```

1. To the vault in development mode, add the default vault address and vault token as follows:

    ```sh
    export VAULT_ADDR="http://localhost:8200"
    export VAULT_TOKEN="root"
    ```

## Run the vault server

1. To run the vault server in development mode, use the `dev` flag. The vault automatically registers the plugin if the directory of the binary of the plugin is provided as an input with the `dev-plugin-dir` flag as follows:

    ```sh
    vault server -dev -dev-root-token-id=root -dev-plugin-dir=<build dir>
    ```

    In case of production mode, set the `dev-root-token` flag which informs the vault to use the default vault token of root to login.

1. Enable the database's secrets as follows:

    ```sh
    vault secrets enable database
    ```

1. For production mode, register the plugin as follows:

    ```sh
    export SHA256=$(sha256sum <build dir>/ysql-plugin  | cut -d' ' -f1)

    vault write sys/plugins/catalog/database/ysql-plugin \
        sha256=$SHA256 \
        command="ysql-plugin"
    ```

1. You can add the database using one of the two following options:

    * Enter the credentials:

        ```sh
        vault write database/config/yugabytedb plugin_name=ysql-plugin  \
        host="127.0.0.1" \
        port=5433 \
        username="yugabyte" \
        password="yugabyte" \
        dbname="yugabyte" \
        load_balance=true \
        yb_servers_refresh_interval=0 \
        allowed_roles="*"
        ```

    * Or, use a connection string:

        ```sh
        vault write database/config/yugabytedb \
        plugin_name=ysql-plugin \
        connection_url="postgres://{{username}}:{{password}}@localhost:5433/yugabyte?sslmode=disable&    load_balance=true&yb_servers_refresh_interval=0" \
        allowed_roles="*" \
        username="yugabyte" \
        password="yugabyte"
        ```

1. Create a role as follows:

    ```sh
    vault write database/roles/my-first-role \
    db_name=yugabytedb \
    creation_statements="CREATE ROLE \"{{username}}\" WITH PASSWORD '{{password}}' VALID UNTIL '{{expiration}}'     NOINHERIT LOGIN; \
        GRANT ALL ON DATABASE \"yugabyte\" TO \"{{username}}\";" \
    default_ttl="1h" \
    max_ttl="24h"
    ```

1. Create a user as follows:

    ```sh
    vault read database/creds/my-first-role
    ```

1. To manage leases for YugabyteDB such as lease lookup, lease renewal, and to revoke the lease, provide the [lease ID](https://developer.hashicorp.com/vault/docs/concepts/lease#lease-ids) along with the following commands:

    ```sh
     # Lease lookup
     vault lease lookup <lease-ID>
    ```

    ```sh
     # Renew the lease
     vault lease renew <lease-ID>
    ```

    ```sh
     #Revoke the lease
     vault lease revoke <lease-ID>
    ```

## Learn more

* [Static roles, root credential rotation, and username customization](https://github.com/yugabyte/hashicorp-vault-ysql-plugin#apart-from-dynamic-roles-ysql-plugin-also-supports-static-roles-root-credential-rotation-and-username-customization)

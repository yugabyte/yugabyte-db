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

[HashiCorp Vault](https://www.hashicorp.com/products/vault) is designed to help organizations manage access to secrets and transmit them safely.

Secrets are any form of sensitive credentials that need to be tightly controlled and monitored and can be used to unlock sensitive information. Secrets can be in the form of passwords, API keys, SSH keys, RSA tokens, or OTP. For more details, refer to [Dynamic secrets](https://github.com/yugabyte/hashicorp-vault-ysql-plugin#dynamic-secrets).

## YSQL plugin for Hashicorp Vault

YSQL plugin for Hashicorp Vault provides APIs for using HashiCorp Vault dynamic secrets with YugabyteDB. The plugin includes APIs for adding YugabyteDB to manage secrets, creating new users, and managing [leases](https://developer.hashicorp.com/vault/docs/concepts/lease).

For more details, refer to [YSQL plugin for Hashicorp Vault](https://github.com/yugabyte/hashicorp-vault-ysql-plugin#ysql-plugin-for-hashicorp-vault-1).

## Setup

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

Install Hashicorp Vault. See [Installing Vault](https://developer.hashicorp.com/vault/docs/install).

Before using Vault, do the following:

- Add go to the path as follows:

    ```sh
    export GOPATH=$HOME/go
    export PATH=$PATH:$GOROOT/bin:$GOPATH/bin
    ```

- To run Vault in [development mode](https://developer.hashicorp.com/vault/docs/concepts/dev-server), add the default vault address and vault token as follows:

    ```sh
    export VAULT_ADDR="http://localhost:8200"
    export VAULT_TOKEN="root"
    ```

- For production mode, register the plugin as follows:

    ```sh
    export SHA256=$(sha256sum <build dir>/ysql-plugin  | cut -d' ' -f1)

    vault write sys/plugins/catalog/database/ysql-plugin \
        sha256=$SHA256 \
        command="ysql-plugin"
    ```

Install the YSQL plugin for Hashicorp Vault as follows:

- Clone the `hashicorp-vault-ysql-plugin` repository:

    ```sh
    git clone https://github.com/yugabyte/hashicorp-vault-ysql-plugin && cd hashicorp-vault-ysql-plugin
    ```

- Build the plugin as follows:

    ```go
    go build -o <build dir>/ysql-plugin cmd/ysql-plugin/main.go
    ```

## Run and configure the vault server

Start the Vault server using the [server](https://developer.hashicorp.com/vault/docs/commands/server) command with the following flags:

- To have Vault automatically register the plugin, provide the path to the directory containing the plugin binary using the `-dev-plugin-dir` flag.
- Set the `-dev-root-token` flag to inform the vault to use the default vault token of root to login (this token is required in production mode).
- To run the server in development mode, use the `-dev` flag; development mode makes it easier to experiment with Vault or start a Vault instance for development.

{{< warning title="Don't run Development mode in production" >}}
Never run development mode in production. It is insecure and will lose data on every restart (as it stores data in memory). Development mode is only suitable for development or experimentation.
{{< /warning >}}

For example, you can start the server as follows:

```sh
vault server -dev -dev-root-token-id=root -dev-plugin-dir=<build directory>
```

Enable the database's secrets as follows:

```sh
vault secrets enable database
```

You can add the database using one of the following options:

- Enter the credentials:

    ```sh
    vault write database/config/yugabytedb plugin_name=ysql-plugin  \
    host="127.0.0.1" \
    port=5433 \
    username="yugabyte" \
    password="yugabyte" \
    db="yugabyte" \
    load_balance=true \
    yb_servers_refresh_interval=0 \
    allowed_roles="*"
    ```

- Use a connection string:

    ```sh
    vault write database/config/yugabytedb \
    plugin_name=ysql-plugin \
    connection_url="postgres://{{username}}:{{password}}@localhost:5433/yugabyte?sslmode=disable&load_balance=true&yb_servers_refresh_interval=0" \
    allowed_roles="*" \
    username="yugabyte" \
    password="yugabyte"
    ```

For more information on running Vault, refer to the [Vault documentation](https://developer.hashicorp.com/vault/docs).

## Use the plugin

Create a role as follows:

```sh
vault write database/roles/my-first-role \
db_name=yugabytedb \
creation_statements="CREATE ROLE \"{{username}}\" WITH PASSWORD '{{password}}' VALID UNTIL '{{expiration}}' NOINHERIT LOGIN; \
    GRANT ALL ON DATABASE \"yugabyte\" TO \"{{username}}\";" \
default_ttl="1h" \
max_ttl="24h"
```

Create a user as follows:

```sh
vault read database/creds/my-first-role
```

To manage leases for YugabyteDB, including lookup, renewal, and revocation, provide the [lease ID](https://developer.hashicorp.com/vault/docs/concepts/lease#lease-ids) along with the following commands:

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

- [Database static roles and credential rotation](https://developer.hashicorp.com/vault/tutorials/db-credentials/database-creds-rotation)
- [Database root credential rotation](https://developer.hashicorp.com/vault/tutorials/db-credentials/database-root-rotation)
- [Username templating](https://developer.hashicorp.com/vault/tutorials/secrets-management/username-templating)
- [YugabyteDB partner page at Hashicorp](https://www.hashicorp.com/partners/tech/yugabyte#all)

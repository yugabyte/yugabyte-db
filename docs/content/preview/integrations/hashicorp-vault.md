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

YSQL plugin provides APIs for using the HashiCorp Vault's dynamic secrets for YugabyteDB.

The plugin can be used to add yugabyteDB to the manage secrets, to create new users, and manage leases.

For more details, refer to [YSQL plugin for Hashicorp Vault]https://github.com/yugabyte/hashicorp-vault-ysql-plugin#ysql-plugin-for-hashicorp-vault-1.

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


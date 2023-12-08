---
title: Developing Secure Applications with Azure Key Vault, Azure SDKs and YugabyteDB
headerTitle: Secure applications using Azure Key Vault, Azure SDKs, and YugabyteDB
linkTitle: Azure Key Vault
description: Developing Secure Applications with Azure Key Vault, Azure SDKs and YugabyteDB
image: /images/tutorials/azure/icons/Key-Vaults-Icon.svg
headcontent: Use Azure SDKs to add services to applications
menu:
  preview:
    identifier: tutorials-azure-key-vault
    parent: tutorials-azure
    weight: 30
type: docs
---

In this tutorial, we'll explore how you can leverage [Azure SDKs](https://azure.microsoft.com/en-us/downloads/) to interact with [Azure services](https://azure.microsoft.com/en-us/products#compute) and use SDKs with database-backed applications, like those running YugabyteDB.

The Azure SDKs are developed in a modular fashion. They provide developers with hooks into the services that their applications rely on, without bloating bundle sizes. The SDKs are developed for parity across many programming languages, but in this example we will use the [JavaScript SDK](https://learn.microsoft.com/en-us/azure/developer/javascript/core/use-azure-sdk) for Node.js.

## Prerequisites

- A [Microsoft Azure](http://azure.microsoft.com) subscription, with [authentication](https://learn.microsoft.com/en-us/azure/developer/javascript/sdk/authentication/local-development-environment-service-principal?tabs=azure-portal)
- An [Azure Key Vault](https://azure.microsoft.com/en-us/products/key-vault)
- [Node.js version](https://github.com/nodejs/release#release-schedule) v18+
- A [YugabyteDB Managed](https://cloud.yugabyte.com/) account with a cluster deployed

## Introduction to the Azure SDKs and Tools

As the Azure Cloud continues to expand and evolve, so do the mechanisms used to deploy and interact with its services. Azure provides well over [150 SDKs](https://learn.microsoft.com/en-us/javascript/api/overview/azure/?view=azure-node-latest), and the list is growing.

For instance, Azure's new [OpenAI SDK](https://github.com/Azure/azure-sdk-for-js/tree/main/sdk/openai/openai) can be used to [build generative AI applications](https://www.yugabyte.com/blog/build-generative-ai-low-latency/).

SDKs provide an interface to interact with Azure services via application code, but there are also [tools](https://azure.microsoft.com/en-us/downloads/) available for download, including a rich command-line interface and a comprehensive set of Visual Studio Code extensions.

Now, let's build a sample application which connects to the [Azure Key Vault](https://azure.microsoft.com/en-us/products/key-vault) from Node.js. We'll then use the secrets stored in this service to connect to a YugabyteDB Managed cluster.

## Example SDK usage

A reference to the application we'll be developing can be found [on GitHub](https://github.com/YugabyteDB-Samples/yugabytedb-azure-key-vault-sdk-demo-nodejs).

1. Initialize a new directory for your project.

    ```sh
    mkdir YBAzureKeyStore && cd YBAzureKeyStore
    npm init -y
    ```

1. Install the [YugabyteDB node-postgres Smart Driver](https://docs.yugabyte.com/preview/drivers-orms/nodejs/yugabyte-node-driver/).

    ```sh
    npm install @yugabytedb/pg
    ```

1. Install the Azure Key Vault secrets client library.

    ```sh
    npm install @azure/keyvault-secrets
    ```

1. Install the Azure Identity client library.

    ```sh
    npm install @azure/identity
    ```

1. Install [Dotenv](https://www.npmjs.com/package/dotenv) and use it to set environment variables.

    ```sh
    npm install dotenv --save-dev
    ```

    ```conf
    // .env
    KEY_VAULT_NAME="[NAME_OF_KEY_VAULT_IN_AZURE]"
    DB_USERNAME="admin"
    DB_PASSWORD="[YB_MANAGED_DB_PASSWORD]"
    //TIP: convert certificate to single line string for ease of use with DB client
    DB_CERTIFICATE="[YB_MANAGED_CERTIFICATE]"
    DB_HOST="[YB_DB_HOST]"
    ```

1. Connect to the Azure Key Store from Node.js.

    ```javascript
    // createSecrets.js

    const { SecretClient } = require("@azure/keyvault-secrets");
    const { DefaultAzureCredential } = require("@azure/identity");

    async function main() {
    // As stated by Microsoft, if you're using MSI, DefaultAzureCredential should "just work".
    // Otherwise, DefaultAzureCredential expects the following three environment variables:
    // - AZURE_TENANT_ID: The tenant ID in Azure Active Directory
    // - AZURE_CLIENT_ID: The application (client) ID registered in the AAD tenant
    // - AZURE_CLIENT_SECRET: The client secret for the registered application
    const credential = new DefaultAzureCredential();

    const keyVaultName = process.env["KEY_VAULT_NAME"];
    if (!keyVaultName) throw new Error("KEY_VAULT_NAME is empty");
    const url = "https://" + keyVaultName + ".vault.azure.net";

    const client = new SecretClient(url, credential);
    ```

1. Use the secret client to set the secrets needed to establish a YugabyteDB Managed connection.

    ```javascript
    // createSecrets.js

    async function main(){
    ...
    // Create secrets for YugabyteDB Managed connection
    await client.setSecret("DBUSERNAME", process.env.DB_USERNAME);
    await client.setSecret("DBPASSWORD", process.env.DB_PASSWORD);
    await client.setSecret("DBCERTIFICATE", process.env.DB_CERTIFICATE);
    await client.setSecret("DBHOST", process.env.DB_HOST);

    // Get secrets to confirm they've been set in Azure Key Vault
    for await (let secretProperties of client.listPropertiesOfSecrets())
    {
        console.log("Secret Name: ", secretProperties.name);
        const secret = await client.getSecret(secretProperties.name);
        console.log("Secret Val:", secret.value);
    }
    }

    main().catch((error) => {
    console.error("An error occurred:", error);
    process.exit(1);
    });
    ```

    ```sh
    node createSecrets.js
    ```

1. Read these secrets from Azure and connect to YugabyteDB Managed.

    ```javascript
    // index.js

    const { Client } = require("@yugabytedb/pg");
    const { SecretClient } = require("@azure/keyvault-secrets");
    const { DefaultAzureCredential } = require("@azure/identity");

    async function main() {
    const credential = new DefaultAzureCredential();

    const keyVaultName = process.env["KEY_VAULT_NAME"];
    if (!keyVaultName) throw new Error("KEY_VAULT_NAME is empty");
    const url = "https://" + keyVaultName + ".vault.azure.net";

    const secretClient = new SecretClient(url, credential);

    const dbhost = await secretClient.getSecret("DBHOST");
    const dbusername = await secretClient.getSecret("DBUSERNAME");
    const dbpassword = await secretClient.getSecret("DBPASSWORD");
    const dbcertificate = await secretClient.getSecret("DBCERTIFICATE");

    const ybclient = new Client({
        database: "yugabyte",
        host: dbhost.value,
        user: dbusername.value,
        port: 5433,
        password: dbpassword.value,
        max: 10,
        idleTimeoutMillis: 0,
        ssl: {
        rejectUnauthorized: true,
        ca: dbcertificate.value,
        servername: dbhost.value,
        },
    });
    console.log("Establishing connection with YugabyteDB Managed");
    await ybclient.connect();
    console.log("Connected successfully.");
    }

    main().catch((error) => {
    console.error("An error occurred:", error);
    process.exit(1);
    });
    ```

    ```sh
    node index.js
    Establishing connection with YugabyteDB Managed...
    Connected successfully.
    ```

Thanks to the Azure Key Vault SDK, we've successfully connected to YugabyteDB, using credentials stored securely in the cloud. This protects our data and ensures a simplified development environment, where credentials can be centrally updated by multiple end users.

The Azure Key Vault SDK comes with additional features for interacting with secrets. These include the ability to update, delete, and purge values if required. You can also use the Azure web console to make any necessary changes.

![Editing secrets in Azure Key Vault.](/images/tutorials/azure/azure-key-vault/azure-key-vault.png "Editing secrets in Azure Key Vault.")

## Wrap-up

We've just scratched the surface with what is possible using Azure SDKs and the surrounding ecosystem. I look forward to developing more applications on Azure in the coming months, as the list of tools continues to expand.

Look out for more of my blogs on Azure and Node.js in the coming weeks. You might also be interested in learning more about [building applications using Azure App Service and YugabyteDB](https://www.yugabyte.com/blog/build-apps-azure-app-service/).

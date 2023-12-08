---
title: How to Develop Azure Functions with YugabyteDB
headerTitle: How to Develop Azure Functions with YugabyteDB
linkTitle: Azure Functions
description: How to Develop Azure Functions with YugabyteDB
image: /images/tutorials/azure/icons/Function-App-Icon.svg
headcontent: How to Develop Azure Functions with YugabyteDB
menu:
  preview:
    identifier: tutorials-azure-functions
    parent: tutorials-azure
    weight: 20
type: indexpage
---

In this tutorial, we’ll guide you through the steps required to develop and deploy a serverless function using Azure Functions and YugabyteDB.

Serverless functions serve many different use cases, including API endpoints, scheduled jobs and file processing. Azure Functions work with a number of [triggers and bindings](https://learn.microsoft.com/en-us/azure/azure-functions/functions-triggers-bindings?tabs=isolated-process%2Cpython-v2&pivots=programming-language-javascript), which allow developers to define precisely when a function will be invoked and how it will interact with other services.

In the following sections, we will:

1. Cover the prerequisites for developing an Azure Function backed by our fully managed DBaaS, [YugabyteDB Managed](https://www.yugabyte.com/managed/)
2. Deploy a database cluster to Azure on YugabyteDB Managed
3. Develop an Azure Function using an HTTP trigger
4. Deploy this serverless function to Azure

Let’s begin by installing the dependencies required to begin effectively developing Azure Functions.

### Prerequisites

- A [Microsoft Azure](http://azure.microsoft.com) subscription, with a resource group and storage account
- Access to the [Azure Functions](https://azure.microsoft.com/en-us/products/functions) resource
- [Node.js version](https://github.com/nodejs/release#release-schedule) v18+
- [Azure Functions Core Tools](https://github.com/Azure/azure-functions-core-tools)
- A [YugabyteDB Managed](https://cloud.yugabyte.com/) account

## What We’ll Build

First, visit GitHub for the [function application](https://github.com/YugabyteDB-Samples/yugabytedb-azure-serverless-functions-demo-nodejs) we will be deploying to Azure.

We’ll develop and deploy an HTTP trigger function, which connects to YugabyteDB and returns the current inventory of our shoe store, YB Shoes.

## Get Started on YugabyteDB Managed

Follow [these instructions](https://docs.yugabyte.com/preview/quick-start-yugabytedb-managed/) to create a YugabyteDB cluster on Azure.

For a configuration that provides fault tolerance across availability zones, deploy a three-node cluster to Azure, in the WestUS3 region. However, you can start with an always-free single-node cluster.

![Deploy a 3-node YugabyteDB Managed cluster to Azure.](/images/tutorials/azure/azure-functions/yb-cluster.png "Deploy a 3-node YugabyteDB Managed cluster to Azure.")

Add the [outbound addresses for your function app](https://learn.microsoft.com/en-us/azure/azure-functions/ip-addresses?tabs=azurecli), which can be found in the networking tab of the Azure portal. This will ensure that a connection can be made between Azure Functions and YugabyteDB.

![Locate outbound IP addresses in the Azure web portal.](/images/tutorials/azure/azure-functions/azure-networking.png "Locate outbound IP addresses in the Azure web portal.")

In addition to the outbound IP addresses in Azure, add your machine’s IP address to the IP Allow List in YugabyteDB Managed, to successfully run your serverless functions locally in development. Now that we have a working cluster in YugabyteDB Managed, let’s add some data.

## Adding Data to YugabyteDB

Now that our cluster is running in the cloud, we can seed it with data using the provided `schema.sql` and `data.sql` files.

1. Use the [YugabyteDB Cloud Shell](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-connect/connect-cloud-shell/) to connect to your cluster.
2. Execute the commands in the `schema.sql` script against your cluster.
3. Execute the commands in the `data.sql` script against your cluster.

With your cluster seeded with data, it's time to build the serverless function to connect to it.

## Serverless Function Development

The Azure Functions Core Tools provide a command-line interface for developing functions on your local machine and deploying them to Azure.

1. Initialize a new Azure Functions project.

    ```sh
    func init YBAzureFunctions --worker-runtime javascript --model V4
    ```

1. Create a new HTTP trigger function.

    ```sh
    cd YBAzureFunctions
    func new --template "Http Trigger" --name GetShoeInventory
    ```

2. Install the YugabyteDB node-postgres Smart Driver.

    ```sh
    npm install @yugabytedb/pg
    ```

3. Update the boilerplate code in _GetShoeInventory.js_.

    ```javascript
    const { app } = require("@azure/functions");
    const { Client } = require("pg");

    app.http("GetShoeInventory", {
    methods: ["GET"],
    authLevel: "anonymous",
    handler: async () => {
        // Read the PostgreSQL connection settings from local.settings.json
        console.log("process.env.DB_HOST:", process.env.DB_HOST);
        const client = new Client({
        user: process.env.DB_USERNAME,
        host: process.env.DB_HOST,
        database: "yugabyte",
        password: process.env.DB_PASSWORD,
        port: 5433,
        max: 10,
        idleTimeoutMillis: 0,
        ssl: {
            rejectUnauthorized: true,
            ca: atob(process.env.DB_CERTIFICATE),
            servername: process.env.DB_HOST,
        },
        });
        try {
        // Connect to the PostgreSQL database
        await client.connect();
        // Query YugabyteDB for shoe inventory
        const query =
            "SELECT i.quantity, s.model, s.brand from inventory i INNER JOIN shoes s on i.shoe_id = s.id;";
        const result = await client.query(query);
        // Process the query result
        const data = result.rows;
        // Close the database connection
        await client.end();
        return {
            status: 200,
            body: JSON.stringify(data),
        };
        } catch (error) {
        console.error("Error connecting to the database:", error);
        return {
            status: 500,
            body: "Internal Server Error",
        };
        }
    },
    });
```

1. Update _local.settings.json_ with the configuration settings required to run the GetShoeInventory function locally.

    ```conf
    # convert the downloaded CA certificate from YugabyteDB Managed to a single line string, then Base64 encode it
    # Azure Configuration Settings forbid special characters, so this ensures the cert can be passed properly to our application
    # Tip: Run this command to convert cert file to base64 encoded single line string:
    # cat /path/to/cert/file | base64

    local.settings.json
    ...
    "DB_USERNAME": "admin",
    "DB_PASSWORD": [YUGABYTE_DB_PASSWORD],
    "DB_HOST": [YUGABYTE_DB_HOST],
    "DB_NAME": "yugabyte",
    "DB_CERTIFICATE": [BASE_64_ENCODED_YUGABYTE_DB_CERTIFICATE]
    ```

2. Run the function locally.

    ```sh
    func start
    ```

Test your function in the browser at [http://localhost:7071/api/GetShoeInventory](http://localhost:7071/api/GetShoeInventory).

It’s now time to deploy our function to Azure.

## Deploy a Function App to Azure

We can deploy our application to Azure using the Azure CLI.

1. Create a Function App.

    ```sh
    az functionapp create --resource-group RESOURCE_GROUP_NAME --consumption-plan-location eastus2 --runtime node --runtime-version 18 --functions-version 4 --name YBAzureFunctions --storage-account STORAGE_ACCOUNT_NAME
    ```

1. Configure the application settings.

    ```sh
    az functionapp config appsettings set -g RESOURCE_GROUP_NAME -n APPLICATION_NAME --setting DB_HOST=[YUGABYTE_DB_HOST] DB_USERNAME=admin DB_PASSWORD=[YUGABYTE_DB_PASSWORD] DB_CERTIFICATE=[BASE_64_ENCODED_YUGABYTE_DB_CERTIFICATE]
    ```

1. Publish Function App to Azure.

    ```sh
    func azure functionapp publish YBAzureFunctions
    ```

1. Verify that the function was published successfully.

    ```sh
    curl https://ybazurefunctions.azurewebsites.net/api/GetShoeInventory

    [{"quantity":24,"model":"speedgoat 5","brand":"hoka one one"},{"quantity":74,"model":"adizero adios pro 3","brand":"adidas"},{"quantity":13,"model":"torrent 2","brand":"hoka one one"},{"quantity":99,"model":"vaporfly 3","brand":"nike"}]
    ```

## Wrapping Up

As you can see, it’s easy to begin developing and publishing database-backed Azure Functions with YugabyteDB. If you’re also interested in building a Node.js web application for YB Shoes using Azure App Service and YugabyteDB, check out the following blog:

- [Build Applications Using Azure App Service and YugabyteDB](https://www.yugabyte.com/blog/build-apps-azure-app-service/)

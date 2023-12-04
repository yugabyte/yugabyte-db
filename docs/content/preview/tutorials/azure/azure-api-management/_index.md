---
title: Developing Centralized APIs Using Azure API Management and YugaybteDB
headerTitle: Developing Centralized APIs Using Azure API Management and YugaybteDB
linkTitle: Azure API Management
description: Developing Centralized APIs Using Azure API Management and YugaybteDB
image: /images/tutorials/azure/icons/API-Management-Icon.svg
headcontent: Developing Centralized APIs Using Azure API Management and YugaybteDB
aliases:
  - /preview/tutorials/azure/api-management
menu:
  preview:
    identifier: tutorials-azure-api-management
    parent: tutorials-azure
type: indexpage
---

[Azure API Management (APIM)](https://azure.microsoft.com/en-us/products/api-management/) can be used to design, manage, and protect your APIs. This service acts as a centralized hub for your APIs, providing API gateway functionality, security measures, rate limiting, analytics, and monitoring, among other features.

In this tutorial, we’ll walk through the steps required to develop and deploy an API using Azure API Management with Node.js and our fully-managed deployment of[ YugabyteDB ](https://www.yugabyte.com/yugabytedb/)(formerly known as YugabyteDB Managed).

In the following sections, we will:

1. Deploy and configure a geo-partitioned YugabyteDB cluster
2. Develop and provision an Azure Function to connect to and query our database in a specific region
3. Create an Azure API Management instance to design an API with an Azure Function backend

**Prerequisites**

- A YugabyteDB account. **Note: **you can sign up for a [free trial of our fully-managed deployment of YugabyteDB](https://cloud.yugabyte.com/signup/).
- An Azure Cloud account with permission to create services

## Getting Started with YugabyteDB

Begin by deploying a multi-region, [geo-partitioned cluster](https://docs.yugabyte.com/preview/develop/build-global-apps/latency-optimized-geo-partition/) in YugabyteDB. This will partition data by region, reducing latencies by fetching data from the closest cluster nodes.

1.  A VPC is required for each region when deploying YugabyteDB on Azure. Create separate VPCs in the \_eastus, westus2, \_and \_westus3 \_regions.
2.  Deploy a 3-node cluster running on Azure, with nodes in the _eastus, westus2, \_and \_westus3 \_regions. Under **Data Distribution**, select \_Partition by region._

![Geo-partitioned YugabyteDB deployment on Azure](/images/tutorials/azure/azure-private-link/yb-deployment.png "Geo-partitioned YugabyteDB deployment on Azure")

3. Enable public access on the cluster and add 0.0.0.0/0 to the cluster’s IP Allow List. This setup permits connections to the cluster from all IP addresses.

   Note: In a production application, [Azure Private Link](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-aws/) can be used with [private service endpoints](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-azure/#create-a-pse-in-yugabytedb-managed) to create a secure connection between your application and database VPCs.

4. Upon creation, save the credentials and [download the CA certificate](https://docs.yugabyte.com/preview/develop/build-apps/cloud-add-ip/#download-your-cluster-certificate) once everything is up and running. This is essential for secure connections using the Node.js Smart Client.

## Creating Tables and Inserting Records

Connect to your YugabyteDB cluster running on Azure via the [Cloud Shell](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-connect/connect-cloud-shell/) and execute the following commands:

1. Create the \_orders \_table and partition it by region.
2. Create partition tables using the automatically created[ regional tablespaces](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-geopartition/).
3. Seed the database with some \_orders. \_These records will be stored in the appropriate cluster node according to the supplied region.

## Developing an Azure Function

Follow the instructions here to [develop and deploy an Azure Function](link to documentation for Azure Functions).

Update the function and its corresponding configuration by doing the following: \

1. Deploy the function to the _uswest3_ region.
2. Use the YugabyteDB host provided for the _westus3 region._
3. Update the contents of the function to GET orders using a connection pool.

This function uses the supplied \_region \_route parameter to determine which database node it should connect to. It then queries the database for orders partitioned in this region.

## How to Get Started with Azure API Management

1. Configure an Azure API Management (APIM) service instance in the \_westus3 \_region.

![API Management instance in westus3 region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-config.png "API Management instance in westus3 region")

2. Add HTTP/2 as a client-side protocol so that you can communicate with the API Management service using HTTP.

![Add http protocol to APIM instance](/images/tutorials/azure/azure-api-management/azure-api-mgmt-http.png "Add http protocol to APIM instance")

3. Review and install your APIM instance. **Note:** This can take around 15-30 minutes depending on Azure’s resources.

## How to Design a REST API in Azure

Azure’s API Management service provides multiple options for API design. Users can create an API from scratch, create or import an API definition, or create an API from an existing Azure resource.

![Designing an API in API Management instance](/images/tutorials/azure/azure-api-management/azure-api-mgmt-designing-api.png "Designing an API in API Management instance")

1. Select the **Function App **option in the **Create from Azure resource **section.
2. Browse for your Azure Function App and create the API in APIM.

![Create API from Azure Function](/images/tutorials/azure/azure-api-management/azure-api-mgmt-function-app.png "Create API from Azure Function")

3. Review your API configuration.

![Review API configuration](/images/tutorials/azure/azure-api-management/azure-api-mgmt-api-overview.png "Review API configuration")

    The _inbound processing _block defines policies to modify a request before it is sent to a backend service. This is where you can set permissions, rate-limiting, and a number of other security features. By default, a subscription key is required to access your APIM API.

## Testing the API

Azure’s API Management Service provides a console for testing your APIs. Supply a value for the _region_ parameters and send a request to verify that the endpoint returns successfully.

### Example 1

![Testing API in westus3 region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-testing-westus3.png "Testing API in westus3 region")

In the case pictured above, the endpoint successfully returns order data from the database nodes in the _westus3 \_region. This endpoint returned in only **3 milliseconds** (see the latency field in the response) because our Azure API Management instance, Azure Function App and the YugabyteDB cluster node, used for this connection all reside in the \_westus3_ region.

Under the hood, this web console executes the following request, using your subscription key for authentication.

### Example 2

Let’s try again, this time testing the \_eastus \_database nodes. The latency will be higher because the API and function instances are fetching data that is stored on database nodes in the east region. However, if you deploy additional API and function instances in the east and request through them, the latency will be as low as you observed previously in the west.

![Testing API in eastus region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-testing-eastus.png "Testing API in eastus region")

## The Wrap-Up

By developing a system with Azure API Management service and function instances in the same region as a geo-partitioned YugabyteDB cluster node, we achieved the lowest latency possible. Furthermore, centralizing your APIs with Azure API Management makes it easy for teams to develop, organize, and secure their endpoints.

If you’re interested in creating a secure connection between Azure and YugabyteDB, you might want to consider the additional resource: \

- [Link to Azure Private Link tutorial]

If you would like to explore the different deployment options of YugabyteDB (including self-managed, co-managed, fully managed, and open source) explore our [database comparison page](https://www.yugabyte.com/compare-products/).

SEO:

SEO Title: How to Develop Centralized APIs With Azure API Management and YugaybteDB

Excerpt (if needed): Check out our step-by-step guide on developing and deploying an API using Azure API Management in conjunction with Node.js and YugabyteDB. It covers the deployment of a geo-partitioned YugabyteDB cluster, the development of Azure Functions for database queries, and the creation of an API using Azure API Management.

Meta description: Learn to deploy APIs using Azure API Management and YugabyteDB, covering geo-partitioned clusters and Azure Function development.

---
title: How to Develop Centralized APIs With Azure API Management and YugaybteDB
headerTitle: Develop centralized APIs using Azure API Management and YugaybteDB
linkTitle: Azure API Management
description: Learn to deploy APIs using Azure API Management and YugabyteDB, covering geo-partitioned clusters and Azure Function development.
image: /images/tutorials/azure/icons/API-Management-Icon.svg
headcontent: Use YugaybteDB as the database backend for your API
menu:
  preview:
    identifier: tutorials-azure-api-management
    parent: tutorials-azure
    weight: 60
type: docs
---

[Azure API Management (APIM)](https://azure.microsoft.com/en-us/products/api-management/) can be used to design, manage, and protect your APIs. This service acts as a centralized hub for your APIs, providing API gateway functionality, security measures, rate limiting, analytics, and monitoring, among other features.

In this tutorial, we'll walk through the steps required to develop and deploy an API using Azure API Management with Node.js and [YugabyteDB Managed](https://www.yugabyte.com/yugabytedb/). It covers the deployment of a geo-partitioned YugabyteDB cluster, the development of Azure Functions for database queries, and the creation of an API using Azure API Management.

In the following sections, you will:

1. Deploy and configure a geo-partitioned YugabyteDB cluster.
1. Develop and provision an Azure Function to connect to and query our database in a specific region.
1. Create an Azure API Management instance to design an API with an Azure Function backend.

## Prerequisites

- A YugabyteDB Managed account. Sign up for a [free trial](https://cloud.yugabyte.com/signup/).
- An Azure Cloud account with permission to create services.

## Create a YugabyteDB cluster

Begin by deploying a multi-region, [geo-partitioned cluster](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-geopartition/) in YugabyteDB. This will partition data by region, reducing latencies by fetching data from the closest cluster nodes.

1. A VPC is required for each region when deploying YugabyteDB on Azure. [Create separate VPCs](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-vpc/) in the _eastus_, _westus2_, and _westus3_ regions.
1. Deploy a [3-node partition by region cluster](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-geopartition/) running on Azure, with nodes in the _eastus_, _westus2_, and _westus3_ regions. Under **Data Distribution**, select **Partition by region**.

    ![Geo-partitioned YugabyteDB deployment on Azure](/images/tutorials/azure/azure-private-link/yb-deployment.png "Geo-partitioned YugabyteDB deployment on Azure")

1. Enable public access on the cluster and add 0.0.0.0/0 to the cluster [IP Allow List](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/). This setup allows connections to the cluster from all IP addresses.

    {{< note title="Note" >}}
In a production application, [Azure Private Link](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-azure/) can be used with [private service endpoints](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-azure/#create-a-pse-in-yugabytedb-managed) to create a secure connection between your application and database VPCs.
    {{< /note >}}

1. Upon creation, save the credentials and [download the CA certificate](../../../develop/build-apps/cloud-add-ip/#download-your-cluster-certificate) once everything is up and running. This is essential for secure connections using the Node.js Smart Client.

## Create tables and insert records

Connect to your YugabyteDB cluster running on Azure via the [Cloud Shell](../../../yugabyte-cloud/cloud-connect/connect-cloud-shell/) and execute the following commands:

1. Create the _orders_ table and partition it by region.
1. Create partition tables using the automatically created [regional tablespaces](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-geopartition/#tablespaces).
1. Seed the database with some orders. These records will be stored in the appropriate cluster node according to the supplied region.

## Develop an Azure function

Follow the instructions in [Develop Azure Functions with YugabyteDB](../azure-functions/) to develop and deploy an Azure Function.

Update the function and its corresponding configuration by doing the following:

1. Deploy the function to the _uswest3_ region.
1. Use the YugabyteDB host provided for the _westus3_ region.
1. Update the contents of the function to GET orders using a connection pool.

This function uses the supplied region route parameter to determine which database node it should connect to. It then queries the database for orders partitioned in this region.

## Create an Azure API Management service

Create the service as follows:

1. Configure an Azure API Management (APIM) service instance in the _westus3_ region.

    ![API Management instance in westus3 region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-config.png "API Management instance in westus3 region")

1. Add HTTP/2 as a client-side protocol so that you can communicate with the API Management service using HTTP.

    ![Add http protocol to APIM instance](/images/tutorials/azure/azure-api-management/azure-api-mgmt-http.png "Add http protocol to APIM instance")

1. Review and install your APIM instance.

    This can take around 15-30 minutes depending on Azure's resources.

## Design a REST API in Azure

Azure's API Management service provides multiple options for API design. You can create an API from scratch, create or import an API definition, or create an API from an existing Azure resource.

![Designing an API in API Management instance](/images/tutorials/azure/azure-api-management/azure-api-mgmt-designing-api.png "Designing an API in API Management instance")

1. Select the **Function App** option in the **Create from Azure resource** section.
1. Browse for your Azure Function App and create the API in APIM.

    ![Create API from Azure Function](/images/tutorials/azure/azure-api-management/azure-api-mgmt-function-app.png "Create API from Azure Function")

1. Review your API configuration.

    ![Review API configuration](/images/tutorials/azure/azure-api-management/azure-api-mgmt-api-overview.png "Review API configuration")

    The _inbound processing_ block defines policies to modify a request before it is sent to a backend service. This is where you can set permissions, rate-limiting, and a number of other security features. By default, a subscription key is required to access your APIM API.

## Test the API

Azure's API Management Service provides a console for testing your APIs. Supply a value for the _region_ parameters and send a request to verify that the endpoint returns successfully.

### westus3

In the following case, the endpoint successfully returns order data from the database nodes in the _westus3_ region.

![Testing API in westus3 region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-testing-westus3.png "Testing API in westus3 region")

This endpoint returned in only **3 milliseconds** (see the latency field in the response) because our Azure API Management instance, Azure Function App, and the YugabyteDB cluster node used for this connection all reside in the _westus3_ region.

Under the hood, this web console executes the following request, using your subscription key for authentication.

### eastus

Let's try again, this time testing the _eastus_ database nodes. The latency is higher because the API and function instances are fetching data that is stored on database nodes in the east region. However, if you deploy additional API and function instances in the east and request through them, the latency will be as low as you observed previously in the west.

![Testing API in eastus region](/images/tutorials/azure/azure-api-management/azure-api-mgmt-testing-eastus.png "Testing API in eastus region")

## Wrap-up

By developing a system with Azure API Management service and function instances in the same region as a geo-partitioned YugabyteDB cluster node, we achieved the lowest latency possible. Furthermore, centralizing your APIs with Azure API Management makes it easy for teams to develop, organize, and secure their endpoints.

To learn how to create a secure connection between Azure and YugabyteDB using Azure Private Link, see [Develop secure applications with Azure Private Link](../azure-private-link/).

If you would like to explore the different deployment options of YugabyteDB (including self-managed, co-managed, fully managed, and open source), see our [database comparison page](https://www.yugabyte.com/compare-products/).

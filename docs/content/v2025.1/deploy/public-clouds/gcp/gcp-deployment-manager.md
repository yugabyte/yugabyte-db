---
title: Deploy YugabyteDB in Google Cloud Platform with GCP Deployment Manager
headerTitle: Google Cloud Platform
linkTitle: Google Cloud Platform
description: Use the GCP Deployment Manager to deploy a YugabyteDB cluster in Google Cloud Platform.
headcontent: Deploy using Google Cloud Deployment Manager
aliases:
  - /v2025.1/deploy/public-clouds/gcp/
  - /v2025.1/deploy/public-clouds/gcp/terraform/
menu:
  v2025.1:
    identifier: deploy-in-gcp-1-deployment-manager
    parent: public-clouds
    weight: 640
type: docs
---

YugabyteDB maintains a Google Cloud Deployment Manager template for deploying YugabyteDB on Google cloud. This automated deployment deploys a multi-zone YugabyteDB universe to three nodes residing in three separate public subnets. The template is in the [Google Cloud Deployment Manager](https://github.com/yugabyte/gcp-deployment-manager) repository.

## Prerequisites

* Download and Install [Google Cloud CLI](https://cloud.google.com/sdk/docs/).
* Clone the [Google Cloud Deployment Manager for YugabyteDB](https://github.com/yugabyte/gcp-deployment-manager.git) repository.

## Deploy using Google Cloud Deployment Manager

[![Open in Google Cloud Shell](https://www.gstatic.com/cloudssh/images/open-btn.svg)](https://console.cloud.google.com/cloudshell/editor?cloudshell_git_repo=https%3A%2F%2Fgithub.com%2Fyugabyte%2Fgcp-deployment-manager.git)

To deploy using Google Cloud Deployment Manager:

1. Change the current directory to the cloned repository.
1. Use the following `gcloud` command to create the deployment-manager deployment:

    ```sh
    $ gcloud deployment-manager deployments create <your-deployment-name> --config=yugabyte-deployment.yaml
    ```

1. Wait 5-10 minutes for the creation of all resources to complete.
1. After the deployment creation is complete, you can describe it as follows:

    ```sh
    $ gcloud deployment-manager deployments describe <your-deployment-name>
    ```

The output includes the YugabyteDB Admin URL, JDBC URL, and YSQL, and YCQL connection strings. You can use the YugabyteDB Admin URL to access the Admin portal.

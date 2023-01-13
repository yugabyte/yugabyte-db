---
title: Deploy YugabyteDB in Google Cloud Platform with GCP Deployment Manager
headerTitle: Google Cloud Platform
linkTitle: Google Cloud Platform
description: Use the GCP Deployment Manager to deploy a YugabyteDB cluster in Google Cloud Platform.
menu:
  stable:
    identifier: deploy-in-gcp-1-deployment-manager
    parent: public-clouds
    weight: 640
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../gcp-deployment-manager/" class="nav-link active">
      <i class="icon-shell"></i>
      Google Cloud Deployment Manager
    </a>
  </li>

  <li>
    <a href="../gke/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Google Kubernetes Engine (GKE)
    </a>
  </li>

  <li >
    <a href="../terraform/" class="nav-link">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>

</ul>

## Prerequisites

* Download and Install [Google Cloud CLI](https://cloud.google.com/sdk/docs/).
* Clone the [Google Cloud Deployment Manager for YugabyteDB](https://github.com/yugabyte/gcp-deployment-manager.git) repository.

## Deploy using Cloud Shell

<a href="https://console.cloud.google.com/cloudshell/editor?cloudshell_git_repo=https%3A%2F%2Fgithub.com%2Fyugabyte%2Fgcp-deployment-manager.git" target="_blank">
    <img src="https://gstatic.com/cloudssh/images/open-btn.svg"/>
</a>

* Change the current directory to the cloned repository.
* Use the following `gcloud` command to create the deployment-manager deployment:

    ```sh
    $ gcloud deployment-manager deployments create <your-deployment-name> --config=yugabyte-deployment.yaml
    ```

* Wait 5-10 minutes for the creation of all resources to complete.
* After the deployment creation is complete, you can describe it as follows:

    ```sh
    $ gcloud deployment-manager deployments describe <your-deployment-name>
    ```

The output includes the YugabyteDB Admin URL, JDBC URL, and YSQL, YCQL, and YEDIS connection strings. You can use the YugabyteDB Admin URL to access the Admin portal.

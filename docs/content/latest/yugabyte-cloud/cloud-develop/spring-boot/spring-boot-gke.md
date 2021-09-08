<!--
title: Deploy a Spring application on GKE
headerTitle: Deploy a Spring application on GKE
linkTitle: Deploy on GKE
description: Deploy a Spring application connected to Yugabyte Cloud on Google Kubernetes Engine (GKE).
menu:
  latest:
    parent: spring-boot
    identifier: spring-boot-gke
    weight: 30
type: page
isTocNested: true
showAsideToc: true
-->

Deploy a Spring application connected to Yugabyte Cloud on Google Kubernetes Engine (GKE) by following the steps below.

This example uses the PetClinic application, connected to Yugabyte Cloud and containerized using Docker; refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/).

## Prerequisites

Before starting, you need to verify that the following are installed and configured:

- Google Cloud Platform (GCP) account
- GCP Cloud SDK
  - For more information, refer to [Cloud SDK](https://cloud.google.com/sdk/).
- Docker

- Your containerized Spring Boot application
  - For more information, refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/)

## Deploy the application image to GKE

### Create a repository in GCP

1. Go to https://console.cloud.google.com/ and sign in to GCP.

1. Type “artifact registry” in the search bar and select **Artifact Registry**.

1. Click **Enable** for the Artifact Registry API.

1. Click **Create Repository**.

1. Give your repository a name, make sure Docker is the selected format, and choose a region. Then click **Create**.

1. Got to your new repository and copy its URL.

### Tag your application image in Docker

On your computer, do the following:

1. Configure Docker to use `gcloud` for authentication. 

    ```sh
    $ gcloud auth configure-docker [region]-docker.pkg.dev
    ```

    Replace [region] with the region you selected in GCP.

    ```output
    Adding credentials for: us-west1-docker.pkg.dev
    After update, the following will be written to your Docker config file
    located at [/Users/gavinjohnson/.docker/config.json]:
    {
      "credHelpers": {
        "us-west1-docker.pkg.dev": "gcloud"
      }
    }

    Do you want to continue (Y/n)?  y

    Docker configuration file updated.
    ```

1. Tag your PetClinic image with your Artifact Registry repo.

    ```sh
    $ docker tag spring-petclinic:latest [repo_url]/spring-petclinic:latest
    ```

    Replace [repo_url] with the URL of your repository.

1. Push your PetClinic image to your repo in GCP.

    ```sh
    $ docker push [repo_url]/spring-petclinic:latest
    ```

    Replace [repo_url] with the URL of your repository.

    ```output
    1dc94a70dbaa: Pushed 
    0d29ec96785e: Pushed 
    888ed16fa8d4: Pushed 
    ...
    ```

Go to your repo in the GCP **Artifact Registry** to view the image you just pushed.

### Deploy the image to GKE

In GCP, do the following:

1. Type “gke” in the search bar and select **Kubernetes Engine**.

1. Click **Enable** for the Kubernetes Engine API.

1. Click **Create**.

1. Click **Configure** under **Autopilot**.

1. Enter a Name, select a Region, and click **Create**.

1. After your cluster has been created, select **Workloads** in the left navigation panel.

1. Click **Deploy**.

1. Select **Existing Container Image** and click **Select** in the Image Path.

1. Click **Artifact Registry**, expand the folders, select the image under `spring-petclinic`, and click **Select**.

1. Click **Add Environment Variable**.

1. Enter the following:

    ```yml
    Key: JAVA_OPTS
    Value: -Dspring.profiles.active=yugabytedb \
    -Dspring.datasource.url=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true \
    -Dspring.datasource.initialization-mode=never
    ```

    Replace `[host]` and `[port]` with the host and port number of your Yugabyte Cloud cluster. To obtain your cluster connection parameters, sign in to Yugabyte Cloud, select your cluster and navigate to [Settings](../../../cloud-clusters/configure-clusters).

1. Click **Continue**.

1. Enter a Name for your application and click **Deploy** (all other options should be auto-filled).

1. After your workload is deployed, click **Expose** on the **Deployment Details**.

1. Set **Port** to 80, **Target Port** to 8080, and **Service Type** to Load Balancer, then click **Expose**.

    After the load balancer is created, the **Service Details** page displays.

1. Click the **External Endpoints** link (http://34.105.123.45/) to navigate to the PetClinic application.

The PetClinic sample application is now connected to your Yugabyte Cloud cluster and running on Kubernetes on GKE.

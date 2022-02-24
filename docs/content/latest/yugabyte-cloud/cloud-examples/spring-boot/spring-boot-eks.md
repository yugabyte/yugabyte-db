<!--
title: Deploy a Spring application on Amazon EKS
linkTitle: Deploy on Amazon EKS
description: Deploy a Spring application connected to Yugabyte Cloud on Amazon Elastic Kubernetes Service (EKS).
menu:
  latest:
    parent: spring-boot
    identifier: spring-boot-eks
    weight: 20
type: page
isTocNested: true
showAsideToc: true
-->

Deploy a Spring application connected to Yugabyte Cloud on Amazon Elastic Kubernetes Service (EKS) by following the steps below.

This example uses the PetClinic application, connected to Yugabyte Cloud and containerized using Docker; refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/).

## Prerequisites

Before starting, you need to verify that the following are installed and configured:

- Amazon Web Services (AWS) account
- AWS Command Line Interface (CLI)
  - For more information, refer to [AWS Command Line Interface](https://aws.amazon.com/cli/).
- Docker

- Your containerized Spring Boot application
  - For more information, refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/)

## Deploy the application image to EKS

### Create a repository in AWS

1. Go to <https://aws.amazon.com/console> and sign in to AWS.

1. Type “ecr” in the search bar and select **Elastic Container Registry** (ECR).

1. Create a repository by clicking **Get Started**.

1. Enter a repository name and click **Create Repository**.

### Push your application image to AWS

On your computer, do the following:

1. Log in to your ECR with Docker.

    ```sh
    $ aws ecr get-login-password --region [aws_region] | docker login --username AWS --password-stdin [aws_acct_id].dkr.ecr.[aws_region].amazonaws.com
    ```

    Replace `[aws_acct_id]` with your AWS account ID, and `[aws_region]` with the region you selected in AWS.

    ```output
    WARNING! Your password will be stored unencrypted in /Users/gavinjohnson/.docker/config.json.
    Configure a credential helper to remove this warning. See
    https://docs.docker.com/engine/reference/commandline/login/#credentials-store

    Login Succeeded
    ```

1. Tag your PetClinic image with your ECR repo.

    ```sh
    $docker tag spring-petclinic:latest [aws_acct_id].dkr.ecr.[aws_region].amazonaws.com/spring-petclinic:latest
    ```

    Replace `[aws_acct_id]` with your AWS account ID, and `[aws_region]` with the region you selected in AWS.

1. Push your PetClinic image to your repo in ECR.

    ```sh
    $ docker push [aws_acct_id].dkr.ecr.[aws_region].amazonaws.com/spring-petclinic:latest
    ```

    Replace `[aws_acct_id]` with your AWS account ID, and `[aws_region]` with the region you selected in AWS.

    ```output
    1dc94a70dbaa: Pushed 
    0d29ec96785e: Pushed 
    888ed16fa8d4: Pushed 
    ...
    ```

Go to your repo in ECR to view the image you just pushed.

### Create the stack in AWS

In AWS, do the following:

1. Type “cloudformation” in the search bar and select **CloudFormation**.

1. Click **Create Stack** and select **With new resources (standard)** to start the **Create Stack** wizard.

1. On the **Create Stack** page, fill out the following details:

    - Under **Prepare template**, select **Template is Ready**. 
    - Under **Template Source**, select Amazon S3 URL. 
    - For **Amazon S3 URL**, enter “https://s3.us-west-2.amazonaws.com/amazon-eks/cloudformation/2020-10-29/amazon-eks-vpc-private-subnets.yaml”. 
    
1. Click **Next**.

1. On the **Specify stack details** page, enter a Stack name, and click **Next**.

1. On the **Configure stack options** page, click **Next**.

1. Review your VPC stack and click **Create Stack**.

### Create the EKS cluster in AWS

After your stack is created, in AWS, do the following:

1. Type “eks” in the search bar and select **Elastic Kubernetes Service**.

1. Under **Create EKS cluster** enter a cluster name and click **Next Step** to start the **Create EKS cluster** wizard.

1. Select a **Cluster Service Role** and click **Next**.

1. On the **Specify networking** page, enter the following:

    - Choose the VPC you just created. This automatically chooses the appropriate subnets. 
    - Select the two security groups that are available. 
    - Under **Cluster Endpoint Access**, choose **Public and private**. 
    
1. Click **Next** to display the **Configure logging** page.

1. Click **Next** to display the **Review and create** page.

1. Review your EKS cluster and click **Create** to create the cluster.

Once the cluster is created, the cluster configuration settings are displayed.

### Add nodes to the cluster in AWS

After your cluster has been created, do the following:

1. Under Cluster configuration, select the **Compute** tab. 

1. Click **Add Node Group** to start the **Add Node Group** wizard.

1. On the **Configure Node Group** page, enter a name for your node group, select a **Node IAM Role**, and click **Next**.

1. On the **Set compute and scaling configuration** page, set compute and scaling options for the node group and click **Next**.

1. On the **Specify networking** page, your subnets are automatically selected. Click **Next**.

1. Review the details of your node group and click **Create**.

### Create the service and deployment

1. On your computer, configure `kubectl` to connect to your EKS cluster.

    ```sh
    $ aws eks --region [aws_region] update-kubeconfig --name [eks_cluster_name]
    ```

    Replace `[aws_region]` with the region you selected in AWS, and `[eks_cluster_name]` with the name of your EKS cluster.

    ```output
    Added new context arn:aws:eks:us-west-1:454529406029:cluster/spring-petclinic to /Users/gavinjohnson/.kube/config
    ```

1. Create a new file named `manifest-eks.yml`, enter the following contents, and save the file:

    ```yml
    apiVersion: v1
    kind: Service
    metadata:
      name: spring-petclinic
      labels:
        run: spring-petclinic
    spec:
      selector:
        app: spring-petclinic
      ports:
        - protocol: TCP
          port: 80
          targetPort: 8080
      type: LoadBalancer
    ---
    apiVersion: apps/v1
    kind: Deployment
    metadata:
      name: spring-petclinic
      labels:
        app: spring-petclinic
    spec:
      replicas: 2
      selector:
        matchLabels:
          app: spring-petclinic
      template:
        metadata:
          labels:
            app: spring-petclinic
        spec:
          containers:
          - name: spring-petclinic
            image: [aws_acct_id].dkr.ecr.[aws_region].amazonaws.com/spring-petclinic:latest
            ports:
              - containerPort: 8080
            env:
            - name: JAVA_OPTS
              value: "-Dspring.profiles.active=yugabytedb -Dspring.datasource.url=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true -Dspring.datasource.initialization-mode=never"
    ```

    Replace `[aws_acct_id]` with your AWS account ID, and `[aws_region]` with the region you selected in AWS. 
    \
    Replace `[host]` and `[port]` with the host and port number of your Yugabyte Cloud cluster. To obtain your cluster connection parameters, sign in to Yugabyte Cloud, select your cluster and navigate to [Settings](../../../cloud-clusters/configure-clusters).

1. Create the Service and Deployment on your EKS cluster.

    ```sh
    $ kubectl create -f manifest-eks.yml
    ```

    ```output
    service/spring-petclinic created
    deployment.apps/spring-petclinic created
    ```

1. Get the URL of the load balancer for the application.

    ```sh
    $ kubectl get svc
    ```

    ```output
    NAME               TYPE           CLUSTER-IP     EXTERNAL-IP                                                               PORT(S)        AGE
    kubernetes         ClusterIP      10.100.0.1     <none>                                                                    443/TCP        20h
    spring-petclinic   LoadBalancer   10.100.30.39   ad7029ef94fed4c06a25897baf9e3c31-1572590571.us-west-1.elb.amazonaws.com   80:30736/TCP   37s
    ```

Go to the External IP address listed in the output to view the application.

The PetClinic application is now connected to your Yugabyte Cloud cluster and running on Kubernetes on EKS.

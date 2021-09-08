<!--
title: Deploy a Spring application using minikube
headerTitle: Deploy a Spring application using minikube
linkTitle: Deploy on minikube
description: Deploy a Spring application connected to Yugabyte Cloud on Kubernetes locally using minikube.
menu:
  latest:
    parent: spring-boot
    identifier: spring-boot-mini
    weight: 40
type: page
isTocNested: true
showAsideToc: true
-->

Deploy a Spring application connected to Yugabyte Cloud on Kubernetes locally using minikube by following the steps below.

This example uses the PetClinic application, connected to Yugabyte Cloud and containerized using Docker; refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/).

## Prerequisites

Before starting, you need to verify that the following are installed and configured:

- `kubectl`
  - For more information, refer to [Install and Set Up kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/).
  - [Kubernetes API v1.18.0](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.18/)
- minikube
  - For more information, refer to [minikube start](https://minikube.sigs.k8s.io/docs/start/).
- Docker

- Your containerized Spring Boot application
  - For more information, refer to [Connect a Spring Boot application](../../../cloud-basics/connect-application/).

## Start minikube

1. Start Docker (or another minkube-compatible container/VM manager) on your computer.

1. Start minikube.

    ```sh
    $ minikube start
    ```

    ```output
    😄  minikube v1.22.0 on Darwin 11.5.1 (arm64)
    ✨  Automatically selected the docker driver
    👍  Starting control plane node minikube in cluster minikube
    🚜  Pulling base image ...
    💾  Downloading Kubernetes v1.21.2 preload ...
        > gcr.io/k8s-minikube/kicbase...: 326.19 MiB / 326.19 MiB  100.00% 10.82 Mi
        > preloaded-images-k8s-v11-v1...: 522.45 MiB / 522.45 MiB  100.00% 15.90 Mi
    🔥  Creating docker container (CPUs=2, Memory=4000MB) ...
    🐳  Preparing Kubernetes v1.21.2 on Docker 20.10.7 ...
        ▪ Generating certificates and keys ...
        ▪ Booting up control plane ...
        ▪ Configuring RBAC rules ...
    🔎  Verifying Kubernetes components...
        ▪ Using image gcr.io/k8s-minikube/storage-provisioner:v5
    🌟  Enabled addons: storage-provisioner, default-storageclass
    🏄  Done! kubectl is now configured to use "minikube" cluster and "default" namespace by default
    ```

1. Open a 2nd terminal and start the minikube dashboard. 

    ```sh
    $ minikube dashboard
    ```

    ```output
    🔌  Enabling dashboard ...
        ▪ Using image kubernetesui/dashboard:v2.1.0
        ▪ Using image kubernetesui/metrics-scraper:v1.0.4
    🤔  Verifying dashboard health ...
    🚀  Launching proxy ...
    🤔  Verifying proxy health ...
    🎉  Opening http://127.0.0.1:51726/api/v1/namespaces/kubernetes-dashboard/services/http:kubernetes-dashboard:/proxy/ in your default browser...
    ```

    This opens the dashboard. Keep this tab open. You’ll use it later to check on your application deployment.

## Deploy the application image to minikube

To deploy the application, you first point your shell to minikube’s Docker daemon. This ensures that Docker and `kubectl` will use your minikube container registry instead of the Docker container registry. Then you create a manifest file and use this to create the service and deployment in minicube.

### Point your shell to minikube

1. In your first terminal, run the following command: 

    ```sh
    $ minikube docker-env
    ```

    ```output
    export DOCKER_TLS_VERIFY="1"
    export DOCKER_HOST="tcp://127.0.0.1:58018"
    export DOCKER_CERT_PATH="/Users/gavinjohnson/.minikube/certs"
    export MINIKUBE_ACTIVE_DOCKERD="minikube"

    # To point your shell to minikube's docker-daemon, run:
    # eval $(minikube -p minikube docker-env)
    ```

1. Run the following command to point your shell to minikube's docker-daemon:

    ```sh
    $ eval $(minikube -p minikube docker-env)
    ```

1. Build your image again.

    ```sh
    $ ./mvnw spring-boot:build-image
    ```

1. Tag your image again.

    ```sh
    $ docker tag [image_id] spring-petclinic
    ```
    
    You can find your image id by running `docker image ls`.

### Create the manifest file and deploy in minikube

1. Create a new file named `manifest-minikube.yml`, enter the following contents, and save it:

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
          port: 8080
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
            image: spring-petclinic
            imagePullPolicy: Never
            ports:
              - containerPort: 8080
            env:
            - name: JAVA_OPTS
              value: "-Dspring.profiles.active=yugabytedb \
              -Dspring.datasource.url=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true \
              -Dspring.datasource.initialization-mode=never"
    ```

    Replace `[host]` and `[port]` with the host and port number of your Yugabyte Cloud cluster. To obtain your cluster connection parameters, sign in to Yugabyte Cloud, select you cluster and navigate to [Settings](../../../cloud-clusters/configure-clusters).

1. Create the service and deployment in minikube using the manifest file.

    ```sh
    $ kubectl create -f manifest-minikube.yml
    ```

    ```output
    service/spring-petclinic created
    deployment.apps/spring-petclinic created
    ```

1. Open a 3rd terminal and externally expose your load balancer.

    ```sh
    $ minikube tunnel
    ```

    ```output
    🏃  Starting tunnel for service spring-petclinic.
    ```

1. Wait for your application to come online (typically several minutes).

1. Go to the local IP and Port you configured in your manifest file (<http://127.0.0.1:8080>).

The PetClinic sample application is now connected to your Yugabyte Cloud cluster and running on Kubernetes locally on minikube.

{{< tip title="Tip" >}}

To reset your minikube setup, you can run the following commands:

```sh
$ kubectl delete -f manifest-minikube.yml
$ minikube stop
$ minikube delete --all
```

{{< /tip >}}

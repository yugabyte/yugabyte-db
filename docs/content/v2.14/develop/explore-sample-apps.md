---
title: Explore YugabyteDB sample applications
headerTitle: Explore sample applications
linkTitle: Explore sample apps
description: Explore sample applications running on YugabyteDB.
image: /images/section_icons/index/develop.png
menu:
  v2.14:
    identifier: explore-sample-apps
    parent: develop
    weight: 581
type: docs
---

After [creating a local cluster](../../quick-start/), follow the instructions on this page to run the Yugastore application.

[Yugastore-java](https://github.com/yugabyte/yugastore-java) app is an end-to-end ecommerce application built using a microservices design pattern. React UI, Spring Boot app framework and YugabyteDB (both YSQL and YCQL) are used as the underlying technology stack.

## Install Yugastore

{{< tabpane text=true >}}

  {{% tab header="**Environment**" disabled=true /%}}

  {{% tab header="macOS" lang="macos" %}}

  1. Clone the repo.

      ```sh
      $ git clone https://github.com/YugabyteDB-Samples/yugastore-java.git
      ```

      ```sh
      $ cd yugastore-java
      ```

  1. Create the app binaries.

      ```sh
      $ mvn -DskipTests package
      ```

  1. Create the app's schema in YugabyteDB.

      ```sh
      $ cd resources
      ```

      ```sh
      $ $YUGABYTE_HOME/bin/ysqlsh -f schema.sql
      ```

      ```sh
      $ $YUGABYTE_HOME/bin/ycqlsh -f schema.cql
      ```

  1. Load the initial data.

      ```sh
      $ ./dataload.sh
      ```

  {{% /tab %}}

  {{% tab header="Linux" lang="linux" %}}

  1. Clone the repo.

      ```sh
      $ git clone https://github.com/YugabyteDB-Samples/yugastore-java.git
      ```

      ```sh
      $ cd yugastore-java
      ```

  1. Create the app binaries.

      ```sh
      $ mvn -DskipTests package
      ```

  1. Create the app's schema in YugabyteDB.

      ```sh
      $ cd resources
      ```

      ```sh
      $ $YUGABYTE_HOME/bin/ysqlsh -f schema.sql
      ```

      ```sh
      $ $YUGABYTE_HOME/bin/ycqlsh -f schema.cql
      ```

  1. Load the initial data.

      ```sh
      $ ./dataload.sh
      ```

  {{% /tab %}}

  {{% tab header="Kubernetes" lang="k8s" %}}

  1. Run the following command:

      ```sh
      $ kubectl run yugastore --image=yugabytedb/yugastore:latest --port=3001 --command -- /usr/local/yugastore/bin/start-for-kubernetes.sh
      ```

  1. Verify the deployment.

      ```sh
      $ kubectl get deployments
      ```

      ```output
      NAME        DESIRED   CURRENT   UP-TO-DATE   AVAILABLE   AGE
      yugastore   1         1         1            1           13m
      ```

  1. Check all the pods.

      ```sh
      $ kubectl get pods
      ```

      ```output
      NAME                        READY     STATUS    RESTARTS   AGE
      yb-master-0                 1/1       Running   0          7h
      yb-master-1                 1/1       Running   0          7h
      yb-master-2                 1/1       Running   0          7h
      yb-tserver-0                1/1       Running   0          7h
      yb-tserver-1                1/1       Running   0          7h
      yb-tserver-2                1/1       Running   0          7h
      yugastore-55d7c6965-ql95t   1/1       Running   0          13m
      ```

  {{% /tab %}}

  {{% tab header="Docker" lang="docker" %}}

  Run the following command:

  ```sh
  $ docker run -p 3001:3001 -d --network yb-net --name yugastore yugabytedb/yugastore
  ```

  {{% /tab %}}

{{< /tabpane >}}

## Start the app

{{< tabpane text=true >}}

  {{% tab header="**Environment**" disabled=true /%}}

  {{% tab header="macOS" lang="macos" %}}

  1. Start the Eureka discovery service. After starting, verify that the service is running at `http://localhost:8761/`.

      ```sh
      $ cd eureka-server-local/ && mvn spring-boot:run
      ```

  1. Start the API gateway microservice.

      ```sh
      $ cd api-gateway-microservice/ && mvn spring-boot:run
      ```

  1. Start the products microservice.

      ```sh
      $ cd products-microservice/ && mvn spring-boot:run
      ```

  1. Start the checkout microservice.

      ```sh
      $ cd checkout-microservice/ && mvn spring-boot:run
      ```

  1. Start the UI service. After starting, browse the app at <http://localhost:8080/>.

      ```sh
      $ cd react-ui/ && mvn spring-boot:run
      ```

  {{% /tab %}}

  {{% tab header="Linux" lang="linux" %}}

  1. Start the Eureka discovery service. After starting, verify that the service is running at `http://localhost:8761/`.

      ```sh
      $ cd eureka-server-local/ && mvn spring-boot:run
      ```

  1. Start the API gateway microservice.

      ```sh
      $ cd api-gateway-microservice/ && mvn spring-boot:run
      ```

  1. Start the products microservice.

      ```sh
      $ cd products-microservice/ && mvn spring-boot:run
      ```

  1. Start the checkout microservice.

      ```sh
      $ cd checkout-microservice/ && mvn spring-boot:run
      ```

  1. Start the UI service. After starting, browse the app at <http://localhost:8080/>.

      ```sh
      $ cd react-ui/ && mvn spring-boot:run
      ```

  {{% /tab %}}

  {{% tab header="Kubernetes" lang="k8s" %}}

  1. Redirect the yugastore pod's port 3001 to the local host. The pod ID (`yugastore-55d7...`) is the last line of the output in the previous step.

      ```sh
      $ kubectl port-forward yugastore-55d7c6965-ql95t 3001
      ```

  1. Open the Yugastore app in your browser at <http://localhost:3001>.

      ![Yugastore app screenshot](/images/develop/realworld-apps/ecommerce-app/yugastore-app-screenshots.png)

  {{% /tab %}}

  {{% tab header="Docker" lang="docker" %}}

  The Yugastore app is already running. Open it in your browser at <http://localhost:3001>.

  ![Yugastore app screenshot](/images/develop/realworld-apps/ecommerce-app/yugastore-app-screenshots.png)

  {{% /tab %}}

{{< /tabpane >}}

## Add items to the cart

1. Add two items to the cart.

    ![yugastore-java checkout](/images/quick_start/binary-yugastore-java-checkout.png)

1. Verify that your cart is now stored inside the YSQL `shopping_cart` table. From your YugabyteDB local cluster home, run the following:

    ```sh
    $ ./bin/ysqlsh
    ```

    ```sql
    yugabyte=# select * from shopping_cart;
    ```

    ```output
         cart_key     | user_id |    asin    |       time_added        | quantity
    ------------------+---------+------------+-------------------------+----------
     u1001-0001048236 | u1001   | 0001048236 | 2019-05-29T13:46:54.046 |        1
     u1001-0001048775 | u1001   | 0001048775 | 2019-05-29T13:46:56.055 |        1
     (2 rows)
    ```

## Verify the completed order

1. Complete the checkout and observe the order number generated.

    ![yugastore-java order confirmation](/images/quick_start/binary-yugastore-java-orderconfirmation.png)

1. Verify that this order number is now in the YCQL `orders` table.

    ```sh
    $ ./bin/ycqlsh localhost
    ```

    ```sh
    ycqlsh> select * from cronos.orders;
    ```

    ```output
     order_id                             | user_id | order_details                                                                                                                                                                           | order_time              | order_total
    --------------------------------------+---------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+-------------------------+-------------
     119b96dc-7d1a-4f2f-a1dd-36def25348e0 |       1 | Customer bought these Items:  Product: The Sherlock Holmes Audio Collection, Quantity: 1; Product: Measure for Measure: Complete &amp; Unabridged, Quantity: 1; Order Total is : 741.95 | 2019-05-29T13:54:04.093 |      741.95
    ```

1. Verify that there are no active shopping carts in YSQL at this point.

    ```sh
    yugabyte=# select * from shopping_cart;
    ```

    ```output
     cart_key | user_id | asin | time_added | quantity
    ----------+---------+------+------------+----------
     (0 rows)
    ```

## Run the IoT Fleet Management app

After running Yugastore, you should run the [IoT Fleet Management](../realworld-apps/iot-spark-kafka-ksql/) app. This app is built on top of YugabyteDB as the database (using the YCQL API), Confluent Kafka as the message broker, KSQL or Apache Spark Streaming for real-time analytics, and Spring Boot as the application framework.

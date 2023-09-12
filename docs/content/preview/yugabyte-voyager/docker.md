<!--
+++
private=true
+++
-->

Before installing yb-voyager, ensure that you have the [Docker](https://docs.docker.com/get-docker/) runtime installed on your machine.

1. Pull the docker image from YugabyteDB's docker hub (Pull the version from docker.io) as follows:

    ```sh
    docker pull yugabytedb/yb-voyager
    ```

1. Download the script to run yb-voyager using the docker image from yb-voyager's GitHub repository, and move it to your machine's bin directory using the following commands:

    ```sh
    wget -O ./yb-voyager https://raw.githubusercontent.com/yugabyte/yb-voyager/main/docker/yb-voyager-docker && chmod +x ./yb-voyager && sudo mv yb-voyager /usr/local/bin/yb-voyager
    ```

    {{< warning >}}
Use yb-voyager docker script without `sudo` to run Voyager commands.
    {{< /warning >}}

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

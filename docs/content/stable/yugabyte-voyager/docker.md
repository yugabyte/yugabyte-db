<!--
+++
private=true
+++
-->

### Prerequisites

Before installing yb-voyager in Docker, ensure that you have the following:

1. [Docker](https://docs.docker.com/get-docker/) runtime installed on your machine.
1. Docker is configured to run without sudo (recommended). Refer to [Manage Docker as a non-root user](https://docs.docker.com/engine/install/linux-postinstall/#manage-docker-as-a-non-root-user) in the Docker documentation.

### Install

Perform the following steps to install yb-voyager:

1. Pull the docker image from YugabyteDB's docker hub (pull the version from docker.io) as follows:

    ```sh
    docker pull software.yugabyte.com/yugabytedb/yb-voyager
    ```

1. Run yb-voyager using one of the following methods:

    #### Method 1: Use the Wrapper script

    1. Download the script to run yb-voyager using the docker image from yb-voyager's GitHub repository, and move it to your machine's bin directory using the following commands:

        ```sh
        wget -O ./yb-voyager https://raw.githubusercontent.com/yugabyte/yb-voyager/main/docker/yb-voyager-docker && \
        chmod +x ./yb-voyager && \
        sudo mv yb-voyager /usr/local/bin/yb-voyager
        ```

    1. Verify the installation:

        ```sh
        yb-voyager version
        ```

    **Limitations:**

    - [Configuration file](/stable/yugabyte-voyager/reference/configuration-file/) is not supported.
    - When using [import-data-file](/stable/yugabyte-voyager/reference/bulk-data-load/import-data-file/), the [import data status](/stable/yugabyte-voyager/reference/data-migration/import-data/#import-data-status) and [end-migration](/stable/yugabyte-voyager/reference/end-migration/) commands do not work.
    - Certain shorthand flags (like `-e`) are not propagated properly to the docker container.

    #### Method 2: Use the Container directly

    Run the container directly with volume mounts.

    1. Run the container with an interactive shell:

        ```sh
        docker run -it --rm \
          --network=host \
          -v /path/to/export-dir/on/host:/home/ubuntu/export-dir \
          yugabytedb/yb-voyager bash
        ```

    1. Once inside the container, run any yb-voyager command. For example, to verify the installation:

        ```sh
        yb-voyager version
        ```

        {{< note title="Mount directories" >}}

  Mount all directories that yb-voyager needs to access (export directory, [configuration files](/stable/yugabyte-voyager/reference/configuration-file/), SSL certificates, and so on). On macOS, add `--platform=linux/amd64` to the `docker run` command.

        {{< /note >}}

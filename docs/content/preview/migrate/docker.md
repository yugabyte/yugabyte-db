<!--
+++
private=true
+++
-->

Before installing yb-voyager, ensure that you have the [Docker](https://docs.docker.com/get-docker/) runtime installed on your machine.

1. Pull the docker image from YugabyteDB's docker hub as follows:

    ```sh
    docker pull yugabytedb/yb-voyager
    ```

1. Download the script to run yb-voyager using the docker image from yb-voyager's GitHub repository, and move it to your machine's bin directory using the following commands:

    ```sh
    wget -O ./yb-voyager https://raw.githubusercontent.com/yugabyte/yb-voyager/main/docker/yb-voyager-docker && chmod +x ./yb-voyager && sudo mv yb-voyager /usr/local/bin/yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

{{< note title = "Note on installing yb-voyager using Docker for Mac" >}}
The script to run the docker image mounts a folder from your host machine to the container. Docker does not have access to all the files on macOS by default. A solution to access all the files is to create a docker volume and then bind that volume to a particular folder (your actual export directory) on your machine. This docker volume's name then becomes your export directory. All the exported files can then be found in the same folder that is binded to this docker volume.

To create a docker volume, run the following docker command:

```sh
docker volume create --driver local \
      --opt type=none \
      --opt device=[PATH_TO_YOUR_EXPORT_DIR] \
      --opt o=bind \
      export-dir
```

The volume created is named `export-dir`. You can use this name as the path by specifying `export-dir` without any path separators for your export directory while running yb-voyager.

{{< /note >}}
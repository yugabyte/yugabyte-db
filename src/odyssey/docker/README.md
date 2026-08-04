### Local development inside Docker

To start you will need a Docker installed. To install see [docker-for-mac](https://docs.docker.com/docker-for-mac/install), [docker-for-windows](https://docs.docker.com/docker-for-windows/install), [docker-for-linux](https://github.com/docker/for-linux).

To start just run:

```bash
docker-compose up
```

This command will:
* Build Docker image with needed dependencies
* Build `CMAKE_BUILD_TYPE` version of Odyssey (`Debug` by default)
* Start Odyssey with `odyssey.conf` from this directory

Feel free to edit `odyssey.conf` – it's for local development and will not be committed.

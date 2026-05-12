# To Build Ubuntu prebuild image

E.g. to build for Ubuntu 22.04, PostgreSQL 16, amd64 and documentdb_0.103.0, run:

```sh
docker build -t <image-tag> -f .github/containers/Build-Ubuntu/Dockerfile_prebuild \ 
    --platform=linux/amd64 --build-arg BASE_IMAGE=ubuntu:22.04 --build-arg POSTGRES_VERSION=16 \ 
    --build-arg DEB_PACKAGE_REL_PATH=packaging/packages/ubuntu22.04-postgresql-16-documentdb_0.103.0_amd64.deb .
```

## To use the image

Step 1: Running the container in detached mode

```sh
docker run -dt <image-tag>
docker exec -it <container-id> bash
```

Step 2: Connect to psql shell

```sh
psql -p 9712 -d postgres
```

## Prebuild Image List

Check the [prebuild image list](./prebuild_image_list.md) for the latest prebuild images.
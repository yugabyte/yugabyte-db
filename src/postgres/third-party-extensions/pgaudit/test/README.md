# Testing

Testing is performed using a Docker container. First build the container:
```
docker build --build-arg UID=$(id -u) --build-arg GID=$(id -g) -f test/Dockerfile.debian -t pgaudit-test .
```
or
```
docker build --build-arg UID=$(id -u) --build-arg GID=$(id -g) -f test/Dockerfile.rhel -t pgaudit-test .
```
Then run the test:
```
docker run --rm -v $(pwd):/pgaudit pgaudit-test /pgaudit/test/test.sh
```

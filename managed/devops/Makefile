.PHONY: docker-test docker-clean docker-run docker-build docker-clear docker-rebuild docker-kill-all

DOCKER_IMAGE_NAME=quay.io/pypa/manylinux2014_x86_64
PWD="`pwd`"

docker-clean:
	sudo rm -rf ./build && sudo rm -rf ./dist && rm -f ./example.spec

docker-run:
	sudo docker run -i -t -v "${PWD}:/code" $(DOCKER_IMAGE_NAME) 
	
docker-build:
	sudo docker build -t $(DOCKER_IMAGE_NAME) .

docker-clear:
	sudo docker rmi -f $(DOCKER_IMAGE_NAME)

docker-rebuild: docker-clear  docker-build

docker-kill-all:
	sudo docker stop $(shell sudo docker ps -a -q)
	sudo docker rm $(shell sudo docker ps -a -q)
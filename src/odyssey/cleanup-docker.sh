#!/bin/bash

set -ex 

while getopts "h:s:" arg; do
  case $arg in
    h)
      echo "usage" 
      ;;
    s)
      skip=$OPTARG
      echo $skip
      ;;
  esac
done


if [ -z $skip ]; then
    docker image ls | grep odyssey | awk '{print $3}' | xargs docker image rm -f || true
    docker rm $(docker stop $(docker ps -aq)) || true
fi

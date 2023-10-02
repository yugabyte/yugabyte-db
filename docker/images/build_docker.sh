#!/usr/bin/env bash
set -eu -o pipefail

IMAGES=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

usage() {
  echo "$0 usage: <arguments> -f <URI for package tarball> <-- docker build opts>"
  echo "  -f <Package URI>  URI to package tarball.  Supports https, s3, scp, and local files"
  echo "  -r <repository>   Docker hub repository.  Default is 'yugabytedb'"
  echo "  -a                Add the arch to the version tag"
  echo "  -l                Tag the image as tag_latest"
  echo "Anything after -- is passed to the docker build command"
}
[ $# -eq 0 ] && usage && exit 1

uri=''
repo_name='yugabytedb'
tag_latest=false
tag_arch=false
while getopts ":hf:r:al" arg; do
  case $arg in
    f) # URI for the yugabyte tarball to dockerize
      uri=${OPTARG}
      ;;
    r) # Docker repo to prefix with which to prefix the image name
      repo_name=${OPTARG}
      ;;
    a) # Add the arch to the tag version info
      tag_arch=true
      ;;
    l) # Enable latest tagging
      tag_latest=true
      ;;
    h | *) # Display help.
      usage
      exit 0
      ;;
  esac
done
# Eat our args so we can pass the rest to docker build command
shift $((OPTIND - 1))

# Detect if we are passing a UBI base image and give some advice if so
# BASE_IMAGE=registry.access.redhat.com/ubi
if [[ "$@" == *"BASE_IMAGE=registry.access.redhat.com/ubi"* ]]; then
  if [[ "$@" != *"USER="* ]]; then
    echo "It is recommended that '--build-arg USER=yugabyte' be used in conjunction with a UBI \
      base image"
    echo "Hit <ctrl-c> now to exit and add the parameter or wait 5 seconds to continue"
    sleep 5
    echo "Proceeding"
  fi
fi

# Infer the workspace from the package name
tarball=$(basename $uri)

#yugabyte-2.18.1.0-b68-almalinux8-aarch64.tar.gz
regex="(.*[^-])-([0-9]+([.][0-9]+){3}-[^-]+)-(.*[^-])-(.*[^.]).tar"
if [[ $tarball =~ $regex ]]; then
  target="${BASH_REMATCH[1]}"
  full_version="${BASH_REMATCH[2]}"
  os="${BASH_REMATCH[4]}"
  arch="${BASH_REMATCH[5]}"
else
  echo "Can't parse $tarball"
  exit 1
fi

if $tag_arch; then
  full_version="${full_version}-${arch}"
fi

# Determine out arch from the passed in package name
docker_arch_arg=''
if [[ $arch != $(uname -m) ]]; then
  case $arch in
    x86_64)
      docker_arch=amd64
      ;;
    aarch64)
      docker_arch=arm64
      ;;
    *)
      echo "Cannot determine arch for docker cross-arch build"
      echo "Host arch: $(uname -m)"
      echo "Package arch: $arch"
      exit 1
      ;;
  esac
  # Add cross compile docker flag.
  docker_arch_arg="--platform linux/${docker_arch}"
fi

# Try to parse some version info
version=${full_version%-*}
build_number=$(tr -d b <<<${full_version#*-})

if [[ ! -d $IMAGES/$target ]]; then
  echo "Docker workspace for ${target} doesn't exist for package ${uri}"
  usage
  exit 1
fi

pkg_dir="$IMAGES/$target/packages"
# This is where we'll copy the URI to
if [[ -d $pkg_dir ]]; then
  echo "Packages directory already exists, deleting"
  echo
  rm -rf $pkg_dir
fi

mkdir -p $pkg_dir
trap "rm -rf $pkg_dir" EXIT

echo "Creating $arch docker image for $target $full_version"
echo "The image will be called ${repo_name}/${target}:${full_version}"
echo

case $uri in
  http*) # Grab the file from a URL
    curl -L -s --output-dir $pkg_dir -O $uri
    ;;
  s3*) # Grab the file from s3
    aws s3 cp --only-show-errors $uri $pkg_dir/
    ;;
  *@*) # Lets try scp
    scp $uri $pkg_dir/
    ;;
  *) # This should be a local file, or at least "local"
    cp $uri $pkg_dir/
esac

# Time to build the image
(set -x;
docker buildx build \
  --build-arg VERSION=$version \
  --build-arg RELEASE=$build_number \
  --tag $repo_name/$target:$full_version \
  --output type=docker \
  $docker_arch_arg \
  $@ $IMAGES/$target)
if $tag_latest; then
  (set -x; docker tag $repo_name/$target:$full_version $repo_name/$target:latest)
fi

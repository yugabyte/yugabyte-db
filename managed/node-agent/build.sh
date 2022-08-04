#!/usr/bin/env bash

set -e

GO111MODULE=on

package_name='node-agent'
platforms=("darwin/amd64" "linux/amd64" "linux/arm64")

base_dir=$(dirname "$0")
pushd "$base_dir"
project_dir=$(pwd)
popd

GOPATH=$project_dir/third-party

build_output_dir="${project_dir}/build"
if [[ ! -d $build_output_dir ]]; then
    mkdir $build_output_dir
fi
grpc_output_dir="${project_dir}/generated"
if [[ ! -d $grpc_output_dir ]]; then
    mkdir $grpc_output_dir
fi
grpc_proto_dir=${project_dir}/proto
grpc_proto_files="${grpc_proto_dir}/*.proto"


generate_grpc_files() {
    protoc -I$grpc_proto_dir --go_out=$grpc_output_dir --go-grpc_out=$grpc_output_dir  \
    --go-grpc_opt=require_unimplemented_servers=false $grpc_proto_files
}

get_executable_name() {
    os=$1
    arch=$2
    executable=${package_name}-${os}-${arch}
    if [ $os == "windows" ]; then
        executable+='.exe'
    fi
    echo "$executable"
}

build_for_platform() {
    os=$1
    arch=$2
    exec_name=$(get_executable_name $os $arch)
    echo "Building ${exec_name}"
    executable="$build_output_dir/$exec_name"
    env GOOS=$os GOARCH=$arch go build -o $executable $project_dir/cmd/cli/main.go
    if [ $? -ne 0 ]; then
        echo "Build failed for $exec_name"
        exit 1
    fi
}

build_for_platforms() {
    build_output_dir=$1
    for platform in "${platforms[@]}"
    do
        platform_split=(${platform//\// })
        os=${platform_split[0]}
        arch=${platform_split[1]}
        build_for_platform $os $arch
    done
}

clean_build() {
    rm -rf $build_output_dir
    rm -rf $grpc_output_dir
}

format() {
    go install github.com/segmentio/golines@latest
    golines -m 100 -t 4 -w $project_dir/adapters/.
    golines -m 100 -t 4 -w $project_dir/app/.
    golines -m 100 -t 4 -w $project_dir/model/.
    golines -m 100 -t 4 -w $project_dir/util/.
}

run_tests() {
    go  test --tags testonly -v $project_dir/adapters/...
    go  test --tags testonly -v $project_dir/app/...
    go  test --tags testonly -v $project_dir/model/...
    go  test --tags testonly -v $project_dir/util/...
}

package_for_platform() {
    os=$1
    arch=$2
    version=$3
    staging_dir_name="node-agent-${version}-${os}-${arch}"
    script_dir="${build_output_dir}/${staging_dir_name}/${version}/scripts"
    bin_dir="${build_output_dir}/${staging_dir_name}/${version}/bin"
    echo "Packaging ${staging_dir_name}"
    os_exec_name=$(get_executable_name $os $arch)
    exec_name="node-agent"
    if [ $os == "windows" ]; then
        exec_name+='.exe'
    fi
    pushd $build_output_dir
    echo "Creating staging directory ${staging_dir_name}"
    rm -rf $staging_dir_name
    mkdir $staging_dir_name
    mkdir -p $script_dir
    mkdir -p $bin_dir
    cp -rf $os_exec_name ${bin_dir}/$exec_name
    pushd "$project_dir/resources"
    cp -rf preflight_check.sh ${script_dir}/preflight_check.sh
    cp -rf yb-node-agent.sh ${bin_dir}/yb-node-agent.sh
    chmod 755 ${script_dir}/*.sh
    chmod 755 ${bin_dir}/*.sh
    popd
    tar -zcf ${staging_dir_name}.tgz -C $staging_dir_name .
    rm -rf $staging_dir_name
    popd
}

package_for_platforms() {
  version=$1
  for platform in "${platforms[@]}"
  do
      platform_split=(${platform//\// })
      os=${platform_split[0]}
      arch=${platform_split[1]}
      package_for_platform $os $arch $version
  done
}

update_dependencies() {
    go mod tidy
}

build=false
clean=false
update_dependencies=false


show_help() {
    cat >&2 <<-EOT

Usage:
./build.sh <fmt|build|clean|test|package <version>|update-dependencies>
EOT
}

build_args=( "$@" )
while [[ $# -gt 0 ]]; do
  case $1 in
    help)
      show_help >&2
      exit 1
      ;;
    fmt)
      fmt=true
      ;;
    build)
      build=true
      ;;
    clean)
      clean=true
      ;;
    test)
      test=true
      ;;
    package)
      package=true
      shift
      # Read the args to package.
      if [[ $# -gt 0 ]]; then
        version=$1
      fi
      ;;
    update-dependencies)
      update_dependencies=true
      ;;
    *)
      echo "Unknown option $1"
      exit 1
      ;;
  esac
  if [[ $# -gt 0 ]]; then
    shift
  fi
done

help_needed=true
if [ "$clean" == "true" ]; then
   help_needed=false
   echo "Cleaning up..."
   clean_build
fi

if [ "$update_dependencies" == "true" ]; then
    help_needed=false
    echo "Updating dependencies..."
    update_dependencies
fi

if [ "$fmt" == "true" ]; then
    help_needed=false
    echo "Formatting..."
    format
fi

if [ "$build" == "true" ]; then
    help_needed=false
    echo "Building..."
    format
    # generate_grpc_files
    build_for_platforms $build_output_dir
fi

if [ "$test" == "true" ]; then
    help_needed=false
    echo "Running tests..."
    run_tests
fi

if [ "$package" == "true" ]; then
    if [ -z "$version"  ]; then
        echo "Version is not specified"
    else
        help_needed=false
        echo "Packaging..."
        package_for_platforms $version
    fi
fi

if [ "$help_needed" == "true" ]; then
    show_help
fi

#!/usr/bin/env bash
# Copyright (c) YugaByte, Inc.

set -euo pipefail

export GO111MODULE=on

readonly protoc_version=21.5
readonly package_name='node-agent'
readonly default_platforms=("linux/amd64" "linux/arm64")
readonly skip_dirs=("third-party" "proto" "generated" "build" "resources" "ybops" "target")

readonly base_dir=$(dirname "$0")
pushd "$base_dir"
readonly project_dir=$(pwd)
popd

export GOPATH=$project_dir/third-party
export GOBIN=$GOPATH/bin
export PATH=$GOBIN:$PATH
mkdir -p "$GOBIN"

readonly build_output_dir="${project_dir}/build"
if [[ ! -d $build_output_dir ]]; then
    mkdir "$build_output_dir"
fi
readonly grpc_output_dir="${project_dir}/generated"
# Python does not support package option for protobuf.
readonly grpc_python_output_dir="${grpc_output_dir}/ybops/node_agent"
readonly grpc_proto_dir="${project_dir}/proto"
readonly grpc_proto_files="${grpc_proto_dir}"/*.proto

to_lower() {
  out=$(awk '{print tolower($0)}' <<< "$1")
  echo "$out"
}

readonly build_os=$(to_lower "$(uname -s)")
readonly build_arch=$(to_lower "$(uname -m)")


setup_protoc() {
    if [ ! -f "$GOBIN"/protoc ]; then
        pushd "$project_dir"
        go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.28
        go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.2
        local release_url=https://github.com/protocolbuffers/protobuf/releases
        local protoc_os=$build_os
        if [ "$protoc_os" = "darwin" ]; then
            protoc_os=osx
        fi
        local protoc_arch=$build_arch
        if [ "$protoc_arch" = "arm64" ]; then
            protoc_arch=aarch_64
        fi
        popd
        local protoc_filename=protoc-${protoc_version}-${protoc_os}-${protoc_arch}.zip
        pushd "$GOPATH"
        curl -fsSLO ${release_url}/download/v"${protoc_version}"/"${protoc_filename}"
        unzip -o "$protoc_filename" -d tmp
        cp -rf tmp/bin/protoc "$GOBIN"/protoc
        rm -rf tmp "$protoc_filename"
        popd
    fi
    which protoc
    protoc --version
    which protoc-gen-go
    protoc-gen-go --version
    which protoc-gen-go-grpc
    protoc-gen-go-grpc --version
}

generate_golang_grpc_files() {
    if [[ ! -d "$grpc_output_dir" ]]; then
        mkdir -p "$grpc_output_dir"
    fi
    protoc -I"$grpc_proto_dir" --go_out="$grpc_output_dir" --go-grpc_out="$grpc_output_dir"  \
    --go-grpc_opt=require_unimplemented_servers=false $grpc_proto_files
}

build_pymodule() {
    if [[ ! -d "$grpc_python_output_dir" ]]; then
        mkdir -p "$grpc_python_output_dir"
    fi
    pushd "$grpc_output_dir"
    python3 -m grpc_tools.protoc -I"$grpc_proto_dir" --python_out="$grpc_python_output_dir" \
    --grpc_python_out="$grpc_python_output_dir" $grpc_proto_files
    # Python does not support custom package. Workaround to change the package name.
    if [ "$build_os" = "darwin" ]; then
        sed -i "" -e 's/^import \(server_pb2.*\) as/from ybops.node_agent import \1 as/' \
        "$grpc_python_output_dir"/*.py
    else
        sed -i -e 's/^import \(server_pb2.*\) as/from ybops.node_agent import \1 as/' \
        "$grpc_python_output_dir"/*.py
    fi
    cp -rf ../ybops "$grpc_output_dir"/
    popd
}

get_executable_name() {
    local os=$1
    local arch=$2
    executable=${package_name}-${os}-${arch}
    if [ "$os" = "windows" ]; then
        executable+='.exe'
    fi
    echo "$executable"
}

prepare() {
    setup_protoc
    generate_golang_grpc_files
}

build_for_platform() {
    local os=$1
    local arch=$2
    exec_name=$(get_executable_name "$os" "$arch")
    echo "Building ${exec_name}"
    executable="$build_output_dir/$exec_name"
    pushd "$project_dir"
    env GOOS="$os" GOARCH="$arch" CGO_ENABLED=0 \
    go build -o "$executable" "$project_dir"/cmd/cli/main.go
    if [ $? -ne 0 ]; then
        echo "Build failed for $exec_name"
        exit 1
    fi
    popd
}

build_for_platforms() {
    for platform in "$@"
    do
        platform_split=(${platform//\// })
        local os=${platform_split[0]}
        local arch=${platform_split[1]}
        build_for_platform "$os" "$arch"
    done
}

clean_build() {
    rm -rf "$build_output_dir"
    rm -rf "$grpc_output_dir"
}



format() {
    pushd "$project_dir"
    go install github.com/segmentio/golines@latest
    go install golang.org/x/tools/cmd/goimports@latest
    for dir in */ ; do
        # Remove trailing slash.
        dir=$(echo "${dir}" | sed 's/\/$//')
        if [[ "${skip_dirs[@]}" =~ "${dir}" ]]; then
            continue
        fi
        golines -m 100 -t 4 -w ./$dir/.
    done
    popd
}

run_tests() {
    # Run all tests if one fails.
    pushd "$project_dir"
    for dir in */ ; do
        # Remove trailing slash.
        dir=$(echo "${dir}" | sed 's/\/$//')
        if [[ "${skip_dirs[@]}" =~ "${dir}" ]]; then
            echo "Skipping directory ${dir}"
            continue
        fi
        echo "Running tests in ${dir}..."
        set +e
        go clean -testcache && go test --tags testonly -v ./"$dir"/...
        set -e
    done
    popd
}

package_for_platform() {
    local os=$1
    local arch=$2
    local version=$3
    staging_dir_name="node_agent-${version}-${os}-${arch}"
    version_dir="${build_output_dir}/${staging_dir_name}/${version}"
    script_dir="${version_dir}/scripts"
    bin_dir="${version_dir}/bin"
    echo "Packaging ${staging_dir_name}"
    os_exec_name=$(get_executable_name "$os" "$arch")
    exec_name="node-agent"
    if [ $os == "windows" ]; then
        exec_name+='.exe'
    fi
    pushd "$build_output_dir"
    echo "Creating staging directory ${staging_dir_name}"
    rm -rf "$staging_dir_name"
    mkdir "$staging_dir_name"
    mkdir -p "$script_dir"
    mkdir -p "$bin_dir"
    cp -rf "$os_exec_name" "${bin_dir}/$exec_name"
    # Follow the symlinks.
    cp -Lf ../version.txt "${version_dir}"/version.txt
    cp -Lf ../version_metadata.json "${version_dir}"/version_metadata.json
    pushd "$project_dir/resources"
    cp -rf preflight_check.sh "${script_dir}"/preflight_check.sh
    cp -rf node-agent-installer.sh "${bin_dir}"/node-agent-installer.sh
    cp -rf ynp "${script_dir}"/ynp
    cp -rf node-agent-provision.sh "${script_dir}"/node-agent-provision.sh
    cp -rf node-agent-provision.yaml "${script_dir}"/node-agent-provision.yaml
    pushd "$project_dir"
    cp -rf ../devops/roles/configure-cluster-server/templates/* \
        "${script_dir}"/ynp/modules/provision/systemd/templates/
    popd
    chmod 755 "${script_dir}"/*.sh
    chmod 755 "${bin_dir}"/*.sh
    popd
    tar -zcf "${staging_dir_name}.tar.gz" -C "$staging_dir_name" .
    popd
}

package_for_platforms() {
  local version=$1
  shift
  for platform in "$@"
  do
      platform_split=(${platform//\// })
      os=${platform_split[0]}
      arch=${platform_split[1]}
      package_for_platform $os $arch $version
  done
}

update_dependencies() {
    pushd "$project_dir"
    go mod tidy -e
    popd
}

# Initialize the vars.
fmt=false
prepare=false
build=false
clean=false
test=false
package=false
version=0
update_dependencies=false
build_pymodule=false


show_help() {
    cat >&2 <<-EOT

Usage:
./build.sh <fmt|prepare|build|clean|test|package <version>|update-dependencies|build-pymodule>
EOT
exit 1
}

while [[ $# -gt 0 ]]; do
  case $1 in
    help)
      show_help >&2
      ;;
    fmt)
      fmt=true
      ;;
    prepare)
      prepare=true
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
    build-pymodule)
      build_pymodule=true
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

NODE_AGENT_PLATFORMS="${NODE_AGENT_PLATFORMS:=}"
# Release build passes the platforms in the environment.
if [ -z "${NODE_AGENT_PLATFORMS}" ]; then
    PLATFORMS=("${default_platforms[@]}")
    echo "Using default platforms ${PLATFORMS[@]}"
else
    PLATFORMS=($(echo "${NODE_AGENT_PLATFORMS}"))
    echo "Using environment platforms ${PLATFORMS[@]}"
fi

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

if [ "$prepare" == "true" ]; then
    help_needed=false
    echo "Preparing..."
    prepare
fi

if [ "$build" == "true" ]; then
    help_needed=false
    echo "Building..."
    pushd "$project_dir"
    if [ ! -f "go.mod" ]; then
        go mod init node-agent
    fi
    popd
    format
    prepare
    build_for_platforms "${PLATFORMS[@]}"
fi

if [ "$test" == "true" ]; then
    help_needed=false
    echo "Running tests..."
    prepare
    run_tests
fi

if [ "$package" == "true" ]; then
    if [ -z "$version"  ]; then
        echo "Version is not specified"
    else
        help_needed=false
        echo "Packaging..."
        package_for_platforms "$version" "${PLATFORMS[@]}"
    fi
fi

if [ "$build_pymodule" == "true" ]; then
    help_needed=false
    echo "Installing python module..."
    build_pymodule
fi

if [ "$help_needed" == "true" ]; then
    show_help
fi

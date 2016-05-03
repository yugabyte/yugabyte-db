log() {
  echo "[$( date +%Y-%m-%dT%H:%M:%S )] $*"
}

if [ "$BASH_SOURCE" == "$0" ]; then
  echo "$BASH_SOURCE must be sourced, not executed" >&2
  exit 1
fi

# This script is expected to be in build-support.
project_dir=$( cd "$( dirname "$BASH_SOURCE" )"/.. && pwd )

if [ ! -d "$project_dir/build-support" ]; then
  echo "Could not determine YB source directory from '$BASH_SOURCE':" \
       "$project_dir/build-support does not exist" >&2
  exit 1
fi

thirdparty_library_path="$project_dir/thirdparty/installed/lib"
thirdparty_library_path+=":$project_dir/thirdparty/installed-deps/lib"
echo "Adding $thirdparty_library_path to library path before the build"
case "$OSTYPE" in
  linux*)
    LD_LIBRARY_PATH+=:$thirdparty_library_path
    export LD_LIBRARY_PATH
  ;;
  darwin*)
    DYLD_FALLBACK_LIBRARY_PATH+=:$thirdparty_library_path
    export DYLD_FALLBACK_LIBRARY_PATH
  ;;
  *)
    echo "Unknown OSTYPE: $OSTYPE" >&2
    exit 1
esac

unset thirdparty_library_path

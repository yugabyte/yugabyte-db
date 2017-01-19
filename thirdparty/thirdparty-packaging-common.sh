# Copyright (c) YugaByte, Inc.

. "${BASH_SOURCE%/*}"/../build-support/common-build-env.sh

if [ "${BASH_SOURCE#*/}" == "${0##*/}" ]; then
  fatal "The script $BASH_SOURCE must be sourced, not invoked."
fi

PREBUILT_THIRDPARTY_S3_URL=s3://binaries.yugabyte.com/prebuilt_thirdparty
PREBUILT_THIRDPARTY_S3_CONFIG_PATH=$HOME/.s3cfg-jenkins-slave
DEFAULT_HASH="DEFAULT_MD5"


compute_md5_hash() {
  local filename=$1
  md5sum "$filename" | awk '{print $1}'
}

get_hash_component() {
  local filename=$1
  echo "$filename" | sed 's/.*HASH_\([^_]*\)_.*/\1/g'
}

replace_default_hash() {
  local filename=$1
  local new_filename

  hash=$(compute_md5_hash "$filename")
  new_filename=${filename//HASH_$DEFAULT_HASH/HASH_$hash}
  mv "$filename" "$new_filename"

  echo $new_filename
}

# We identify systems that we can share library builds between using a "system configuration
# string" of the following form:
#   - OS name (e.g. Linux, Mac OS X)
#   - OS version (e.g. Ubuntu 15.10, Mac OS X 10.11.3)
get_system_conf_str() {

  local cpu_arch=$( uname -m )
  local os_name
  local os_version
  local hash=$DEFAULT_HASH

  case "$( uname )" in
    Linux)
      os_name="Linux"
      if [[ -f "/etc/centos-release" ]]; then
        os_version=$(
          cat /etc/centos-release | \
            sed 's/[()]/ /g; s/release 7[.][0-9][0-9]*[.][0-9][0-9]* /release 7/g;'
        )
      else
        # os_version will be something like "Ubuntu_15.10"
        os_version=$( cat /etc/issue | sed 's/\\[nl]/ /g' )
        if [[ "$os_version" != Ubuntu* ]]; then
          fatal "Unexpected contents of /etc/issue: '$os_version', should start with Ubuntu"
        fi
      fi
      os_version=$( echo $os_version )  # normalize spaces
    ;;
    Darwin)
      os_name="Mac_OS_X"
      # This will be something like "10.11.3" on El Capitan.
      os_version=$( sw_vers -productVersion )
    ;;
    *)
      fatal "Unknown operating system: $( uname )"
  esac
  if [ -z "$os_version" ]; then
    fatal "Failed to determine OS version"
  fi
  local os_version=$( echo "$os_version" | sed 's/ /_/g' )

  echo "${os_name}_${os_version}_${cpu_arch}_HASH_${hash}"
}

get_prebuilt_thirdparty_name_prefix() {
  system_conf_str=$( get_system_conf_str )
  if [ -z "$system_conf_str" ]; then
    fatal "Failed to determine a 'system configuration string'" \
          "consiting of OS name, version, and architecture."
  fi
  echo "yb_prebuilt_thirdparty__${system_conf_str}__"
}

download_prebuilt_thirdparty_deps() {
  # In this function, "fatal" is an error condition that terminates the entire build, but "return 1"
  # simply means we're skipping pre-built third-party dependency download and will build those
  # dependencies from scratch.

  if [ -z "${TP_DIR:-}" ]; then
    fatal "The 'thirdparty' directory path TP_DIR is not set"
  fi

  if [ -n "${YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY:-}" ]; then
    fatal "YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY is defined, not attempting to download prebuilt" \
          "third-party dependencies."
  fi

  local skipped_msg_suffix="not attempting to download prebuilt third-party dependencies"
  if [ ! -f "$PREBUILT_THIRDPARTY_S3_CONFIG_PATH" ]; then
    log "S3 configuration file $PREBUILT_THIRDPARTY_S3_CONFIG_PATH not found, $skipped_msg_suffix"
    return 1
  fi

  if ! which s3cmd >/dev/null; then
    log "s3cmd not found, $skipped_msg_suffix"
    return 1
  fi

  local name_prefix=$( get_prebuilt_thirdparty_name_prefix )
  if [ -z "$name_prefix" ]; then
    fatal "Unable to compute name prefix for pre-built third-party dependencies package"
  fi
  # Replace default hash with * to be s3cmd friendly, as we do not know the hash upfront.
  name_prefix=$( echo "$name_prefix" | sed "s/HASH_${DEFAULT_HASH}__/HASH_/g" )
  local s3cmd_cmd_line_prefix=( s3cmd -c "$PREBUILT_THIRDPARTY_S3_CONFIG_PATH" )
  local s3cmd_ls_cmd_line=( "${s3cmd_cmd_line_prefix[@]}" )
  s3cmd_ls_cmd_line+=( --list-md5 ls "$PREBUILT_THIRDPARTY_S3_URL/$name_prefix*" )
  echo "Listing pre-built third-party dependency packages: ${s3cmd_ls_cmd_line[@]}"
  local s3cmd_ls_output=( $( "${s3cmd_ls_cmd_line[@]}" | sort | tail -1 ) )
  echo "s3cmd ls output: ${s3cmd_ls_output[@]}"
  local package_s3_url=${s3cmd_ls_output[4]}
  if [[ ! "$package_s3_url" =~ ^s3://.*[.]tar[.]gz$ ]]; then
    log "Expected the pre-built third-party dependency package URL obtained via 's3cmd ls'" \
      "to start with s3:// and end with .tar.gz, found: '$package_s3_url'"
    return 1
  fi
  local remote_md5_sum=$(get_hash_component "$package_s3_url" )
  if [[ ! "$remote_md5_sum" =~ ^[0-9a-f]{32}$ ]]; then
    log "Expected to see an MD5 sum, found '$remote_md5_sum' in '$remote_md5_sum'"
    return 1
  fi
  local package_name=${package_s3_url##*/}
  local download_dir="$TP_DIR/prebuilt_downloads"
  mkdir -p "$download_dir"
  local need_to_download=true
  local dest_path=$download_dir/$package_name
  if [ -f "$dest_path" ]; then
    local_md5_sum=$( compute_md5_hash "$dest_path" )
    if [ "$local_md5_sum" == "$remote_md5_sum" ]; then
      echo "Local file $dest_path matches the remote package's MD5 sum, not downloading"
      need_to_download=false
    else
      echo "Local file $dest_path MD5 sum: $local_md5_sum, remote MD5 sum: $remote_md5_sum," \
        "re-downloading from '$package_s3_url'"
      rm -f "$dest_path"
    fi
  else
    echo "Local file $dest_path not found, downloading from $package_s3_url"
  fi
  if "$need_to_download"; then
    local s3cmd_get_cmd_line=( "${s3cmd_cmd_line_prefix[@]}" )
    s3cmd_get_cmd_line+=( get "$package_s3_url" "$download_dir" )
    ( set -x; "${s3cmd_get_cmd_line[@]}" )
  fi

  pushd "$TP_DIR" >/dev/null
  echo "Extracting '$dest_path' into '$PWD'"
  tar xzf "$dest_path"

  popd >/dev/null
}

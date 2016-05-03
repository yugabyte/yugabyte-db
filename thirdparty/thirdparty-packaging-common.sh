# Copyright (c) YugaByte, Inc.

if [ "${BASH_SOURCE#*/}" == "${0##*/}" ]; then
  echo "The script $BASH_SOURCE must be sourced, not invoked." >&2
  exit 1
fi

PREBUILT_THIRDPARTY_S3_URL=s3://binaries.yugabyte.com/prebuilt_thirdparty
PREBUILT_THIRDPARTY_S3_CONFIG_PATH=$HOME/.s3cfg-jenkins-slave


# We identify systems that we can share library builds between using a "system configuration
# string" of the following form:
#   - OS name (e.g. Linux, Mac OS X)
#   - OS version (e.g. Ubuntu 15.10, Mac OS X 10.11.3)
get_system_conf_str() {

  local cpu_arch=$( uname -m )
  local os_name
  local os_version

  case "$( uname )" in
    Linux)
      os_name="Linux"
      # os_version will be something like "Ubuntu_15.10"
      os_version=$( cat /etc/issue | sed 's/\\[nl]/ /g' )
      if [[ "$os_version" != Ubuntu* ]]; then
        echo "Only Ubuntu is currently supported, found /etc/issue: '$os_version'" >&2
        exit 1
      fi
      os_version=$( echo $os_version )  # normalize spaces
    ;;
    Darwin)
      os_name="Mac_OS_X"
      # This will be something like "10.11.3" on El Capitan.
      os_version=$( sw_vers -productVersion )
    ;;
    *)
      echo "Unknown operating system: $( uname )" >&2
      exit 1
  esac
  if [ -z "$os_version" ]; then
    echo "Failed to determine OS version" >&2
    exit 1
  fi
  local os_version=$( echo "$os_version" | sed 's/ /_/g' )

  echo "${os_name}_${os_version}_${cpu_arch}"
}

get_prebuilt_thirdparty_name_prefix() {
  system_conf_str=$( get_system_conf_str )
  if [ -z "$system_conf_str" ]; then
    echo "Failed to determine a 'system configuration string'" \
      "consiting of OS name, version, and architecture in ${FUNCNAME[0]}" >&2
    exit 1
  fi
  echo "yb_prebuilt_thirdparty__${system_conf_str}__"
}

download_prebuilt_thirdparty_deps() {
  if [ -z "${TP_DIR:-}" ]; then
    echo "The 'thirdparty' directory path TP_DIR is not set in ${FUNCNAME[0]}" >&2
    exit 1
  fi
  local skipped_msg_suffix="not attempting to download prebuilt third-party dependencies"
  if [ ! -f "$PREBUILT_THIRDPARTY_S3_CONFIG_PATH" ]; then
    echo "S3 configuration file $PREBUILT_THIRDPARTY_S3_CONFIG_PATH not found, $skipped_msg_suffix"
    return 1
  fi

  if ! which s3cmd >/dev/null; then
    echo "s3cmd not found, $skipped_msg_suffix"
    return 1
  fi

  local name_prefix=$( get_prebuilt_thirdparty_name_prefix )
  if [ -z "$name_prefix" ]; then
    echo "Unable to compute name prefix for pre-built third-party dependencies package" >&2
    exit 1
  fi
  local s3cmd_cmd_line_prefix=( s3cmd -c "$PREBUILT_THIRDPARTY_S3_CONFIG_PATH" )
  local s3cmd_ls_cmd_line=( "${s3cmd_cmd_line_prefix[@]}" )
  s3cmd_ls_cmd_line+=( --list-md5 ls "$PREBUILT_THIRDPARTY_S3_URL/$name_prefix*" )
  echo "Listing pre-built third-party dependency packages: ${s3cmd_ls_cmd_line[@]}"
  local s3cmd_ls_output=( $( "${s3cmd_ls_cmd_line[@]}" | sort | tail -1 ) )
  echo "s3cmd ls output: ${s3cmd_ls_output[@]}"
  local remote_md5_sum="${s3cmd_ls_output[3]}"
  if [[ ! "$remote_md5_sum" =~ ^[0-9a-f]{32}$ ]]; then
    echo "Expected to see an MD5 sum, found '$remote_md5_sum' in ${FUNCNAME[0]}" >&2
    exit 1
  fi
  local package_s3_url=${s3cmd_ls_output[4]}
  if [[ ! "$package_s3_url" =~ ^s3://.*[.]tar[.]gz$ ]]; then
    echo "Expected the pre-built third-party dependency package URL obtained via 's3cmd ls'" \
      "to start with s3:// and end with .tar.gz, found: '$package_s3_url'" >&2
    exit 1
  fi
  local package_name=${package_s3_url##*/}
  local download_dir="$TP_DIR/prebuilt_downloads"
  mkdir -p "$download_dir"
  local need_to_download=true
  local dest_path=$download_dir/$package_name
  if [ -f "$dest_path" ]; then
    local_md5_sum=$( md5sum "$dest_path" | awk '{print $1}' )
    if [ "$local_md5_sum" == "$remote_md5_sum" ]; then
      echo "Local file $dest_path matches the remote package's MD5 sum, not downloading"
      need_to_download=false
    else
      echo "Local file $dest_path MD5 sum: $local_md5_sum, remote MD5 sum: $remote_md5_sum," \
        "re-downloading from '$package_s3_url'"
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

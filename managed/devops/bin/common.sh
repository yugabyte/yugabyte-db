#@IgnoreInspection BashAddShebang
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Common Bash code for the devops repository.
# Include this in a script as follows:
#
# . "${BASH_SOURCE%/*}"/common.sh
#
# Explanation: this will remove the last filename component (everything following the last '/')
# in the script path and add common.sh. We are assuming that the script is located within the same
# directory as this file.

set -euo pipefail

if [[ $BASH_SOURCE == $0 ]]; then
  fatal "$BASH_SOURCE must be sourced, not invoked as a script"
fi


# -------------------------------------------------------------------------------------------------
# Functions used when initializing constants
# -------------------------------------------------------------------------------------------------

# Make a regular expression from a list of possible values. This function takes any non-zero number
# of arguments, but each argument is further broken down into components separated by whitespace,
# and those components are treated as separate possible values. Empty values are ignored.
# This function should be the same as in yugabyte/build-support/common-build-env.sh.
regex_from_list() {
  local regex=""
  # No quotes around $@ on purpose: we want to break arguments containing spaces.
  for item in $@; do
    if [[ -z $item ]]; then
      continue
    fi
    if [[ -n $regex ]]; then
      regex+="|"
    fi
    regex+="$item"
  done
  echo "^($regex)$"
}

set_python_executable() {
  PYTHON_EXECUTABLE=""
  executables=( "${PYTHON3_EXECUTABLES[@]}" )
  if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "0" ]]; then
    executables=( "${PYTHON2_EXECUTABLES[@]}" )
  fi

  for py_executable in "${executables[@]}"; do
    if which "$py_executable" > /dev/null 2>&1; then
      PYTHON_EXECUTABLE="$py_executable"
      export PYTHON_EXECUTABLE
      return
    fi
  done

  if which python > /dev/null 2>&1; then
    if python -c 'import sys; sys.exit(1) if sys.version_info[0] != 2 else sys.exit(0)';  then
      if [[ "$YB_MANAGED_DEVOPS_USE_PYTHON3" == "0" ]]; then
        PYTHON_EXECUTABLE="python"
        export PYTHON_EXECUTABLE
        return
      fi
    elif [[ "$YB_MANAGED_DEVOPS_USE_PYTHON3" == "1" ]]; then
      PYTHON_EXECUTABLE="python"
      export PYTHON_EXECUTABLE
      return
    fi
  fi

  echo "Failed to find python executable."
  exit 1
}

# -------------------------------------------------------------------------------------------------
# Constants
# -------------------------------------------------------------------------------------------------
readonly PYTHON2_EXECUTABLES=('python2' 'python2.7')
readonly PYTHON3_EXECUTABLES=('python3.8' 'python3.11' 'python3.10' 'python3.9' 'python3')
readonly PYTHON3_VERSIONS=('python3.8' 'python3.9' 'python3.10' 'python3.11')
readonly LINUX_PLATFORMS=('manylinux2014_x86_64-cp-38-cp38' 'manylinux2014_x86_64-cp-39-cp39' \
                         'manylinux2014_x86_64-cp-310-cp310' 'manylinux2014_x86_64-cp-311-cp311')
readonly MACOS_PLATFORMS=('macosx-10.10-x86_64-cp-38-cp38' 'macosx-10.10-x86_64-cp-39-cp39' \
                         'macosx-10.10-x86_64-cp-310-cp310' 'macosx-10.10-x86_64-cp-311-cp311')
DOCKER_PEX_IMAGE_NAME="yba-devops-pex-builder"
DOCKER_VENV_IMAGE_NAME="yba-devops-venv-builder"
PYTHON_EXECUTABLE=""

readonly YB_MANAGED_DEVOPS_USE_PYTHON3=${YB_MANAGED_DEVOPS_USE_PYTHON3:-1}
if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 != "0" &&
      $YB_MANAGED_DEVOPS_USE_PYTHON3 != "1" ]]; then
  fatal "Invalid value of YB_MANAGED_DEVOPS_USE_PYTHON3: $YB_MANAGED_DEVOPS_USE_PYTHON3," \
        "expected 0 or 1"
fi

set_python_executable

readonly yb_script_name=${0##*/}
readonly yb_script_name_no_extension=${yb_script_name%.sh}

readonly yb_devops_home=$( cd "${BASH_SOURCE%/*}"/.. && pwd )
if [[ ! -d $yb_devops_home/roles ]]; then
  fatal "No 'roles' subdirectory found inside yb_devops_home ('$yb_devops_home')"
fi

if [ -L /opt/yugabyte/devops ]; then
 export yb_devops_home_link="/opt/yugabyte/devops"
fi

# We need to export yb_devops_home because we rely on it in ansible.cfg.
export yb_devops_home

# We need this in addition to yb_devops_home, because we sometimes just install a subset of scripts
# from the devops/bin directory onto a remote machine, and the directory they are in is not called
# "bin", so we can't assume they are located at "$yb_devops_home/bin".
readonly devops_bin_dir=$( cd "${BASH_SOURCE%/*}" && pwd )

readonly VALID_CLOUD_TYPES=(
  aws
  gcp
)
readonly VALID_CLOUD_TYPES_RE=$( regex_from_list "${VALID_CLOUD_TYPES[@]}" )
readonly VALID_CLOUD_TYPES_STR="${VALID_CLOUD_TYPES[@]}"

set +u
readonly MANAGED_PYTHONPATH_ORIGINAL="${PYTHONPATH:-}"
readonly MANAGED_PATH_ORIGINAL="${PATH:-}"
set -u

# Basename (i.e. name excluding the directory path) of our virtualenv.
if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "1" ]]; then
  readonly YB_VIRTUALENV_BASENAME=venv
  readonly YB_PEXVENV_BASENAME=pexvenv
  readonly REQUIREMENTS_FILE_NAME="$yb_devops_home/python3_requirements.txt"
  readonly FROZEN_REQUIREMENTS_FILE="$yb_devops_home/python3_requirements_frozen.txt"
  readonly YB_PYTHON_MODULES_DIR="$yb_devops_home/python3_modules"
  readonly YB_PYTHON_MODULES_PACKAGE="$yb_devops_home/python3_modules.tar.gz"
  readonly YB_INSTALLED_MODULES_DIR="$yb_devops_home/python3_installed_modules"
else
  readonly YB_VIRTUALENV_BASENAME=python_virtual_env
  readonly YB_PEXVENV_BASENAME=pexvenv
  readonly REQUIREMENTS_FILE_NAME="$yb_devops_home/python_requirements.txt"
  readonly FROZEN_REQUIREMENTS_FILE="$yb_devops_home/python_requirements_frozen.txt"
  readonly YB_PYTHON_MODULES_DIR="$yb_devops_home/python2_modules"
  readonly YB_PYTHON_MODULES_PACKAGE="$yb_devops_home/python2_modules.tar.gz"
  readonly YB_INSTALLED_MODULES_DIR="$yb_devops_home/python2_installed_modules"
fi

readonly YBOPS_TOP_LEVEL_DIR_BASENAME=opscli
readonly YBOPS_PACKAGE_NAME=ybops

readonly NODE_AGENT_HOME="$yb_devops_home/opscli/ybops/node_agent"
readonly NODE_AGENT_SRC_DIR="$yb_devops_home/../node-agent"

# -------------------------------------------------------------------------------------------------
# Functions
# -------------------------------------------------------------------------------------------------

log_empty_line() {
  echo >&2
}

log_warn() {
 local _log_level="warn"
 log "$@"
}

log_error() {
 local _log_level="error"
 log "$@"
}

# This just logs to stderr.
log() {
  BEGIN_COLOR='\033[0;32m'
  END_COLOR='\033[0m'
  GREEN='\033[0;32m'
  RED='\033[0;31m'

  case ${_log_level:-info} in
    error)
      BEGIN_COLOR='\033[0;31m'
      shift
      ;;
    warn)
      BEGIN_COLOR='\033[0;33m'
      shift
      ;;
  esac
  echo -e "${BEGIN_COLOR}[$( get_timestamp ) ${BASH_SOURCE[1]##*/}:${BASH_LINENO[0]} ${FUNCNAME[1]}]${END_COLOR}" $* >&2
}

fatal() {
  log "$@"
  exit 1
}

get_timestamp() {
  date +%Y-%m-%d_%H_%M_%S
}

ensure_log_dir_defined() {
  if [[ -z ${log_dir:-} ]]; then
    fatal "log_dir is not set"
  fi
}

ensure_log_dir_exists() {
  ensure_log_dir_defined
  mkdir -p "$log_dir"
}

configure_standard_log_path() {
  ensure_log_dir_exists
  log_path="$log_dir/${yb_script_name_no_extension}_$( get_timestamp ).log"
  show_log_path "$log_path"
}

show_log_path() {
  ensure_log_dir_defined
  if [[ "${log_path#$log_dir/}" == "$log_dir" ]]; then
    fatal "Expected log path '$log_path' to start with log directory '$log_dir/'"
  fi
  heading "Logging to $log_path" >&2
}

show_log_path_in_the_end() {
  heading "Log saved to $log_path" >&2
}

run_ybcloud() {
  ( set -x; "$devops_bin_dir/ybcloud.sh" "$@" )
}

heading() {
  echo
  echo --------------------------------------------------------------------------------------------
  echo "$1"
  echo --------------------------------------------------------------------------------------------
  echo
}

get_current_timestamp()
{
  date +%Y-%m-%dT%H:%M:%S
}

deactivate_virtualenv() {
  if ! should_use_virtual_env; then
    return
  fi

  if [[ -d "$YB_INSTALLED_MODULES_DIR" ]]; then
    export PYTHONPATH=$MANAGED_PYTHONPATH_ORIGINAL
    export PATH=$MANAGED_PATH_ORIGINAL
    return
  fi

  # Deactivate virtualenv if it is already activated.
  # The VIRTUAL_ENV variable is defined whenever we are using a Python virtualenv. It is set by
  # the bin/activate script.
  if [[ -n ${VIRTUAL_ENV:-} ]] && [[ -f "$VIRTUAL_ENV/bin/activate" ]]; then
    local _old_virtual_env_dir=$VIRTUAL_ENV
    set +u
    # Ensure we properly "activate" the virtualenv and import all its Bash functions.
    . "$VIRTUAL_ENV/bin/activate"
    # The "deactivate" function is defined by virtualenv's "activate" script.
    deactivate
    set -u

    # The deactivate function does not remove virtualenv's bin path from PATH (as of 09/26/2016),
    # do it ourselves.

    # Add leading/trailing ":" so we can handle the virtualenv directory being in the beginning,
    # end, or in the middle of PATH uniformly while removing the old virtualenv's "bin" directory.
    PATH=:$PATH:
    PATH=${PATH//:$_old_virtual_env_dir\/bin:/:}  # Remove the virtualenv directory from PATH.
    PATH=${PATH#:}  # Remove the leading ":" we've added.
    PATH=${PATH%:}  # Remove the trailing ":" we've added.
    export PATH

    unset VIRTUAL_ENV
    unset PYTHONPATH
    set_python_executable
  fi
}

activate_virtualenv() {
  if ! should_use_virtual_env; then
    return
  fi

  if [[ -d "$YB_INSTALLED_MODULES_DIR" ]]; then
    export PYTHONPATH="${YB_INSTALLED_MODULES_DIR}:${MANAGED_PYTHONPATH_ORIGINAL}"
    export PATH="${YB_INSTALLED_MODULES_DIR}/bin:${MANAGED_PATH_ORIGINAL}"
    export SITE_PACKAGES="$YB_INSTALLED_MODULES_DIR"
    return
  fi

  if [[ ! $virtualenv_dir = */$YB_VIRTUALENV_BASENAME ]]; then
    fatal "Internal error: virtualenv_dir ('$virtualenv_dir') must end" \
          "with YB_VIRTUALENV_BASENAME ('$YB_VIRTUALENV_BASENAME')"
  fi
  if [[ ! -d $virtualenv_dir ]]; then
    # We need to be using system python to install the virtualenv module or create a new virtualenv.
    deactivate_virtualenv
    if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "0" ]]; then
      pip_install "virtualenv<20"
    fi

    (
      set -x
      cd "${virtualenv_dir%/*}"
      if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "1" ]]; then
        $PYTHON_EXECUTABLE -m venv "$YB_VIRTUALENV_BASENAME"
      else
        # Assuming that the default python binary is pointing to Python 2.7.
        $PYTHON_EXECUTABLE -m virtualenv --no-setuptools "$YB_VIRTUALENV_BASENAME"
      fi
    )
  elif [[ ${is_linux} == "true" ]]; then
    deactivate_virtualenv
  fi

  if [[ ! -f "$virtualenv_dir/bin/activate" ]]; then
    fatal "File '$virtualenv_dir/bin/activate' does not exist."
  fi

  set +u
  . "$virtualenv_dir"/bin/activate
  set -u
  export SITE_PACKAGES=$(python -c "import sysconfig; print(sysconfig.get_path('purelib'))")
  PYTHON_EXECUTABLE="python"
  log "Using virtualenv python executable now."

  # We unset the pythonpath to make sure we aren't looking at the global pythonpath.
  unset PYTHONPATH
}

create_pymodules_package() {
  rm -rf "$YB_PYTHON_MODULES_DIR"
  mkdir -p "$YB_PYTHON_MODULES_DIR"
  extra_install_flags=""
  if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "0" ]]; then
    extra_install_flags="setuptools<45"
  fi
  run_pip install --upgrade pip > /dev/null
  # Download the scripts necessary (i.e. ansible). Remove the modules afterwards to avoid
  # system-specific libraries.
  log "Downloading package scripts"
  run_pip install $extra_install_flags -r "$FROZEN_REQUIREMENTS_FILE" \
    --prefix="$YB_PYTHON_MODULES_DIR" --ignore-installed
  run_pip install $extra_install_flags "$yb_devops_home/$YBOPS_TOP_LEVEL_DIR_BASENAME" \
    --prefix="$YB_PYTHON_MODULES_DIR" --ignore-installed
  rm -rf "$YB_PYTHON_MODULES_DIR"/lib*
  # Download remaining libraries.
  log "Downloading package libraries"
  run_pip install $extra_install_flags -r "$FROZEN_REQUIREMENTS_FILE" \
    --target="$YB_PYTHON_MODULES_DIR" --ignore-installed
  run_pip install $extra_install_flags "$yb_devops_home/$YBOPS_TOP_LEVEL_DIR_BASENAME" \
    --target="$YB_PYTHON_MODULES_DIR" --ignore-installed
  # Change shebangs to be path-independent.
  current_py_exec=$(which $PYTHON_EXECUTABLE)
  LC_ALL=C find "$YB_PYTHON_MODULES_DIR"/bin ! -name '*.pyc' -type f -exec sed -i.yb_tmp \
    -e "1s|${current_py_exec}|/usr/bin/env ${PYTHON_EXECUTABLE}|" {} \; -exec rm {}.yb_tmp \;
  tar -C $(dirname "$YB_PYTHON_MODULES_DIR") -czvf "$YB_PYTHON_MODULES_PACKAGE" \
    $(basename "$YB_PYTHON_MODULES_DIR")
  rm -rf "$YB_PYTHON_MODULES_DIR"
}

install_pymodules_package() {
  rm -rf "$YB_INSTALLED_MODULES_DIR"
  mkdir -p "$YB_INSTALLED_MODULES_DIR"
  tar -C "$YB_INSTALLED_MODULES_DIR" -xvf "$YB_PYTHON_MODULES_PACKAGE" --strip-components=1
  # Create startup script that python will execute beforehand. This is to properly add all python
  # modules to path (e.g. google-api-core).
  cat > "$YB_INSTALLED_MODULES_DIR"/sitecustomize.py << EOF
import site
site.addsitedir("$YB_INSTALLED_MODULES_DIR")
EOF
}

# Somehow permissions got corrupted for some files in the virtualenv, possibly due to sudo
# installations of Python packages. While it is unclear if the root cause of this is still present,
# we proactively fix these before installing Python requirements.
fix_virtualenv_permissions() {
  if ! should_use_virtual_env; then
    return
  fi
  if [[ $USER != "root" ]]; then
    if [[ -z $( find "$virtualenv_dir" ! -user "$USER" ) ]]; then
      # Avoid asking for sudo password unless necessary.
      log "All files and directories in '$virtualenv_dir' are already owned by $USER," \
          "no permissions to fix."
    else
      log "Changing ownership of '$virtualenv_dir' to $USER"
      sudo chown -R "$USER" "$virtualenv_dir"
    fi
  fi
}

delete_virtualenv() {
  if ! should_use_virtual_env; then
    return
  fi
  if [[ -n "$virtualenv_dir" && -d "$virtualenv_dir" ]]; then
    log "Deleting the virtualenv at '$virtualenv_dir'."
    rm -rf "$virtualenv_dir"
  else
    log "No virtualenv found at '$virtualenv_dir', nothing to delete."
  fi
}


not_installed() {
  if which "$1" >/dev/null; then
    log "$1 is already installed, skipping"
    return 1
  else
    return 0
  fi
}

verbose_cmd() {
  (
    set -x
    "$@"
  )
}

# Create the given directory and all its parent directories if it does not
# exist and log a message in that case.
verbose_mkdir_p() {
  if [[ $# -ne 1 ]]; then
    fatal "verbose_mkdir_p expects exactly one argument (directory path to create)"
  fi
  local d="$1"
  if [[ ! -d $d ]]; then
    ( set -x; mkdir -p "$d" )
  fi
}

run_pip() {
  if [[ $YB_MANAGED_DEVOPS_USE_PYTHON3 == "1" ]]; then
    "$PYTHON_EXECUTABLE" -m pip "$@"
  else
    $PYTHON_EXECUTABLE "$(which pip2.7)" "$@"
  fi
}

pip_install() {
  local module_name=""
  case $# in
    1)
      module_name="$1"
      ;;
    2)
      if [[ ! -f $2 ]]; then
        fatal "Python requirements file '$2' does not exist."
      fi
      if [[ $1 != "-r" ]]; then
        fatal "The pip_install function expects -r as the first argument when given two" \
              "arguments, got: $*"
      fi
      ;;
    *)
      fatal "The pip_install function takes 1 arg (module name) or 2 args (-r REQUIREMENT_FILE)"
      ;;
  esac

  if is_virtual_env; then
    log "Installing Python module(s) inside virtualenv."
    (
      verbose_cmd run_pip install "$@"
    )
  elif [[ -n $module_name && -n $( run_pip show "$module_name" ) ]]; then
    log "Python module $module_name already installed, not upgrading."
  else
    log "Installing Python module(s) outside virtualenv, using --user."

    run_pip install --user "$@"
  fi
}

install_pip() {
  deactivate_virtualenv
  if ! which pip >/dev/null; then
    log "Installing python-pip (will need sudo privileges for that)..."
    if [[ ${is_debian} == "true" ]]; then
      # Need pip to install Python dependencies.
      # http://docs.ansible.com/ansible/guide_gce.html
      sudo apt-get install python-pip
    elif [[ ${is_centos} == "true" ]]; then
      # TODO: can the two commands below be done as one command? Or does the
      # second one need to run separately because it needs to refresh the
      # package index?
      sudo yum install -y epel-release
      sudo yum install -y python-pip
    elif [[ ${is_mac} == "true" ]]; then
      sudo easy_install pip
    else
      fatal "Don't know how to install pip on this OS. OSTYPE=$OSTYPE"
    fi
  else
    log "It looks like pip (Python module manager) is already installed, skipping"
  fi
}

add_to_bashrc() {
  if [[ $# -ne 1 ]]; then
    fatal "add_to_bashrc takes exactly one argument, the command to add"
  fi
  local cmd_to_add="$1"

  if [[ ! -f ~/.bashrc ]]; then
    touch ~/.bashrc
  fi

  if ! grep "$cmd_to_add" ~/.bashrc; then
    log "Adding command to ~/.bashrc: $cmd_to_add"
    (
      echo
      echo "$cmd_to_add"
    ) >>~/.bashrc
  fi
}

virtualenv_aware_log() {
  if is_virtual_env; then
    log "[virtualenv '$VIRTUAL_ENV']" "$@"
  else
    log "$@"
  fi
}

install_ybops_package() {
  activate_virtualenv
  virtualenv_aware_log "Installing the $YBOPS_PACKAGE_NAME package"
  local user_flag=""
  if ! is_virtual_env; then
    user_flag="--user"
  fi
  log "Using python: $( which $PYTHON_EXECUTABLE )"
  # This is invoked outside of the source tree to install venv.
  if [[ -f "$NODE_AGENT_SRC_DIR/build.sh" ]]; then
    $NODE_AGENT_SRC_DIR/build.sh build-pymodule
    rm -rf "$NODE_AGENT_HOME"
    cp -rf "$NODE_AGENT_SRC_DIR/generated/ybops/node_agent" "$NODE_AGENT_HOME"
  fi
  (
    cd "$yb_devops_home/$YBOPS_TOP_LEVEL_DIR_BASENAME"
    $PYTHON_EXECUTABLE setup.py install $user_flag
    rm -rf build dist "$YBOPS_PACKAGE_NAME.egg-info"
  )
  virtualenv_aware_log "Installed the $YBOPS_PACKAGE_NAME package"
}

is_virtual_env() {
  [[ -n ${VIRTUAL_ENV:-} ]]
}

should_use_virtual_env() {
  [[ -z ${YB_NO_VIRTUAL_ENV:-} ]]
}

detect_os() {
  is_mac=false
  is_linux=false
  is_debian=false
  is_ubuntu=false
  is_centos=false

  case $(uname) in
    Darwin) is_mac=true ;;
    Linux) is_linux=true ;;
    *)
      fatal "Unknown operating system: $(uname)"
  esac

  if [[ ${is_linux} == "true" ]]; then
    # Detect Linux flavor
    if [[ -f /etc/issue ]] && grep Ubuntu /etc/issue >/dev/null; then
      is_debian=true
      is_ubuntu=true
    elif [[ -f /etc/redhat-release ]] && grep CentOS /etc/redhat-release > /dev/null; then
      is_centos=true
    fi
    # TODO: detect other Linux flavors, including potentially non-Ubuntu Debian distributions
    # (if we ever need it).
  fi
}

# Function that is needed to activate the PEX environment. Note that using the PEX requires that
# the PEX exists at the provided PEX_PATH location (which should happen automatically as part of
# the Devops release generation process). Exports PEX_EXTRA_SYS_PATH when the PEX is used during the
# execution of ybcloud.sh or pywrapper (needed to import node_client_utils.py when running
# run_node_action.py). Also export PEX_PATH, SITE_PACKAGES, and SCRIPT_PATH, so that they
# can be picked up by ybcloud.sh and py_wrapper to run Python scripts with the PEX env.
activate_pex() {
  PEX_EXTRA_SYS_PATH="$yb_devops_home/opscli:$yb_devops_home/bin:$yb_devops_home/opscli/ybops"
  export PEX_EXTRA_SYS_PATH
  # Used by other devops scripts
  PEX_PATH="$yb_devops_home/pex/pexEnv"
  export PEX_ROOT="$pex_venv_dir"
  SCRIPT_PATH="$yb_devops_home/opscli/ybops/scripts/ybcloud.py"
  export SCRIPT_PATH
  export ANSIBLE_CONFIG="$yb_devops_home/ansible.cfg"
  trap "set +e rm -f $pex_lock" EXIT
  # Create and activate virtualenv
  (
    flock 9 || log "Waiting for pex lock";
    if [[ ! -d $pex_venv_dir && -d $PEX_PATH ]]; then
      deactivate_virtualenv
      PEX_TOOLS=1 $PYTHON_EXECUTABLE $PEX_PATH venv $pex_venv_dir
    fi
  ) 9>>"$pex_lock"

  if [[ ! -f "$pex_venv_dir/bin/activate" ]]; then
    log "File '$pex_venv_dir/bin/activate' does not exist."
    exit 0
  fi
  set +u
  . "$pex_venv_dir"/bin/activate
  set -u
  PYTHON_EXECUTABLE="python"
  log "Using pex virtualenv python executable now."
  unset PYTHONPATH

}

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

detect_os

#
# We should not load up ansible.env in all our shells scripts anymore! This should be automatically
# sourced in our env, or manually sourced in individial scripts that need credentials to be setup.
# Otherwise, for production scripts, run from YW, they should get all the relevant vars from YW
# directly!
#

export ANSIBLE_HOST_KEY_CHECKING=False
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

log_dir=$HOME/logs

readonly virtualenv_dir=$yb_devops_home/$YB_VIRTUALENV_BASENAME
readonly pex_venv_dir=$yb_devops_home/$YB_PEXVENV_BASENAME
readonly pex_lock="/tmp/pexlock"

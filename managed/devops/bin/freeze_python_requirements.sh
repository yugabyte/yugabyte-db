#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

. "${BASH_SOURCE%/*}"/common.sh

SETUPTOOLS_PY38_VERSION="72.2.0"
DEEPDIFF_PY38_VERSION="5.5.0"
PIP_PY38_VERSION="25.0.1"
PIP_VERSION="25.3"

if [[ ! ${1:-} =~ ^(-y|--yes)$ ]]; then
  echo >&2 "This will remove and re-create the entire virtualenv from ${virtualenv_dir} in order"
  echo >&2 "to re-generate 'frozen' python dependency versions. It is only necessary to run this"
  echo >&2 "script once in a while, when we want to upgrade versions of some third-party Python"
  echo >&2 "modules."
  echo >&2
  echo >&2 "The frozen Python requirements file will be generated at: $FROZEN_REQUIREMENTS_FILE"
  echo >&2 -n "Continue? [y/N] "

  read confirmation

  if [[ ! $confirmation =~ ^(y|Y|yes|YES)$ ]]; then
    log "Operation canceled."
    exit 1
  fi
fi

delete_virtualenv
activate_virtualenv

cd "$yb_devops_home"

log "There should be no pre-installed Python modules in the virtualenv"
( set -x; run_pip list )

log "Upgrading pip to latest version"
(set -x; run_pip install --upgrade pip)

log "Installing Python modules according to the ${REQUIREMENTS_FILE_NAME} file"
( set -x; run_pip install -r "${REQUIREMENTS_FILE_NAME}" )

log "Generating $FROZEN_REQUIREMENTS_FILE"

# Use LANG=C to force case-sensitive sorting.
# https://stackoverflow.com/questions/10326933/case-sensitive-sort-unix-bash
( set -x; run_pip freeze --all | LANG=C sort >"$FROZEN_REQUIREMENTS_FILE" )

# Handle the different setup tool version requirements (78.1.1 is only on 3.9 and up)
sed -ie "s/\(setuptools.*==.*\)/\1;python_version >= '3.9'/" $FROZEN_REQUIREMENTS_FILE
echo "setuptools==${SETUPTOOLS_PY38_VERSION};python_version < '3.9'" >> $FROZEN_REQUIREMENTS_FILE

# Handle the different deepdiff version requirements (8.6.1 is only on 3.9 and up)
sed -ie "s/\(deepdiff.*==.*\)/\1;python_version >= '3.9'/" $FROZEN_REQUIREMENTS_FILE
echo "deepdiff==${DEEPDIFF_PY38_VERSION};python_version < '3.9'" >> $FROZEN_REQUIREMENTS_FILE

# Handle the different pip version requirements (25.3 is only on 3.9 and up)
sed -ie "s/\(pip.*==.*\)/pip==${PIP_VERSION};python_version >= '3.9'/" $FROZEN_REQUIREMENTS_FILE
echo "pip==${PIP_PY38_VERSION};python_version < '3.9'" >> $FROZEN_REQUIREMENTS_FILE

log_empty_line
log "Contents of $FROZEN_REQUIREMENTS_FILE:"
cat "$FROZEN_REQUIREMENTS_FILE"

# This will validate that exactly the right set of packages is installed, and install ybops.
set -x
"$yb_devops_home/bin/install_python_requirements.sh"

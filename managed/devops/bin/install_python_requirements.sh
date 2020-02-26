#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

. "${0%/*}/common.sh"

show_usage() {
  cat <<-EOT
Usage: ${0##*/} [<options>]
Options:
  --no-virtual-env
    Do not use a virtualenv. Instead, use the --user flag when installing modules. Note that if a
    virtualenv is already activated outside of this script, it will still be used.
  -h, --help
    Show usage
EOT
}

while [[ $# -gt 0 ]]; do
  case $1 in
    --no-virtual-env)
      export YB_NO_VIRTUAL_ENV=1
    ;;
    -h|--help)
      show_help >&2
      exit 1
    ;;
    *)
      fatal "Invalid option: $1"
  esac
  shift
done

if should_use_virtual_env; then
  activate_virtualenv
  fix_virtualenv_permissions
fi

# looks like there is some issue with setuptools and virtualenv on python2.
# https://github.com/pypa/virtualenv/issues/1493, adding this requirement
pip_install "setuptools<45"
pip_install -r "$FROZEN_REQUIREMENTS_FILE"
# Shorten the length of the shebang so it doesn't crash in our Jenkins pipeline.
find $virtualenv_dir/bin -type f -exec sed -i \
  -e "s|${virtualenv_dir}/bin/python|/usr/bin/env python|" {} \;

log "Installing ybops package"
install_ybops_package


if should_use_virtual_env; then
  log "Expecting there to be no differences between the output of 'pip freeze' and the contents" \
      "of $FROZEN_REQUIREMENTS_FILE"
  if grep -Fxvf <(run_pip freeze | grep -v ybops) "$FROZEN_REQUIREMENTS_FILE" >/dev/null; then
    log_warn "WARNING: discrepancies found between the contents of '$FROZEN_REQUIREMENTS_FILE'" \
             "and what's installed in the virtualenv $virtualenv_dir."
    log_error "Showing full diff output, but please ignore extra modules that were installed."
    # Use "LANG=C" to enforce case-sensitive sorting.
    # https://stackoverflow.com/questions/10326933/case-sensitive-sort-unix-bash
    diff <( LANG=C sort <"$FROZEN_REQUIREMENTS_FILE" ) \
         <(run_pip freeze | grep -v ybops | LANG=C sort)
    exit 1
  else
    log "Successfully validated the set of installed Python modules in the virtualenv" \
        "$virtualenv_dir."
  fi
fi

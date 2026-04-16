#!/usr/bin/env bash
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# Simple linter to make sure a file is uniquely sorted.
set -u

. "${BASH_SOURCE%/*}/common.sh"

pattern='YB|Yb|yb'

if [[ "$1" == */yb_typedefs.list ]]; then
  grep -Env "$pattern" "$1" \
    | sed 's/^/error:missing_yb_in_type_name:'\
'Types in yb_typedefs.list should have "yb":/'

  grep -no '^Form_[a-zA-Z0-9_]*' "$1" \
    | while IFS=: read -r lineno form; do
    formdata="FormData_${form#Form_}"
    if ! grep -q "^$formdata$" "$1"; then
      echo "error:missing_formdata:$formdata is missing for $form:$lineno:"
    fi
  done

  grep -no '^FormData_[a-zA-Z0-9_]*' "$1" \
    | while IFS=: read -r lineno formdata; do
    form="Form_${formdata#FormData_}"
    if ! grep -q "^$form$" "$1"; then
      echo "error:missing_form:$form is missing for $formdata:$lineno:"
    fi
  done
  
  # Find all header files in pggate that contain YB_DEFINE_HANDLE_TYPE
  git grep -l YB_DEFINE_HANDLE_TYPE src/yb/yql/pggate | \
    # Extract the handle type names
    xargs grep -ho 'YB_DEFINE_HANDLE_TYPE([A-Z][a-zA-Z0-9_]*)' | \
    # Remove the macro name and parentheses
    sed 's/YB_DEFINE_HANDLE_TYPE(//' | sed 's/)//' | sort -u | while read -r handle_type; do
      transformed_type="Ybc${handle_type}"
      if ! grep -q "^$transformed_type$" "$1"; then
        echo "error:missing_handle_type:Missing $transformed_type \
for YB_DEFINE_HANDLE_TYPE($handle_type):1:"
      fi
    done
else
  grep -En "$pattern" "$1" \
    | sed 's/^/error:bad_yb_in_type_name:'\
'Types in non-yb_typedefs.list should not have "yb":/'
fi

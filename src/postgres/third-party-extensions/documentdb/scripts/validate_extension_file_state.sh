#!/bin/bash
set -e -u


source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

repoScriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

function ValidateNoCitusReferences()
{
    local _dir_to_check=$1

    # TODO: Clean these up
    validationExceptions="documentdb--0.24-0.sql create_index_background--0.23-0.sql"
    validationExceptions="$validationExceptions current_op.c ttl_support_functions--0.24-0.sql"
    validationExceptions="$validationExceptions diagnostic_commands_common.c"

    foundInvalid="";
    for queryStr in "citus_" "pg_dist" "run_command_on_all_nodes" "run_command_on_coordinator"; do
        for f in `grep -r -l -i -E $queryStr --include "*.h" --include "*.c" --include "*.sql" --include "*.rs" $_dir_to_check`; do
            fileName=$(basename $f)
            if [[ $validationExceptions =~ $fileName ]]; then
                echo "Allowing $f as an exception for $queryStr"
                continue;
            fi;

            echo "Remove references to $queryStr in $f - Please introduce a distributed hook to manage citus function execution"
            foundInvalid="true"
        done
    done

    if [ "$foundInvalid" != "" ]; then
        echo "Found invalid citus references"
        exit 1
    fi
}


function CheckSqlLatestVersionMatchesLatestFile()
{
    local _script_dir_prefix=$1
    # check if the latest snapshot matches the latest 'version'
    for script_dir in $repoScriptDir/../$_script_dir_prefix/sql/udfs/*; do
        # Get the "latest" sql files:
        for script_file in $( find "$script_dir" -iname "*--latest.sql" ); do
            latest_file=$( basename $script_file );
            file_base=${latest_file%--*};

            # We want to find the last snapshotted sql file, to make sure it's the same
            # as "latest.sql". This is done by:
            # 1. Getting the filenames in the UDF directory (using find instead of ls, to keep shellcheck happy)
            # 2. Filter out latest.sql
            # 3. Sort using "version sort"
            # 4. Get the last one using tail
            latest_snapshot=$(\
                find "$script_dir" -iname "$file_base--*.sql" -exec basename {} \; \
                | { grep --invert-match latest.sql || true; } \
                | sort --version-sort \
                | tail --lines 1);
            echo "Checking $latest_file against $latest_snapshot"

            diff --unified "$script_dir/$latest_file" "$script_dir/$latest_snapshot";
        done
    done
}

# Ensure that citus references are done via hooks and not in the SQL/C files
ValidateNoCitusReferences $repoScriptDir/../pg_documentdb_core
ValidateNoCitusReferences $repoScriptDir/../pg_documentdb
ValidateNoCitusReferences $repoScriptDir/../pg_documentdb_gw

# Validate sql files for the purposes of review and sanity.
CheckSqlLatestVersionMatchesLatestFile pg_documentdb_core
CheckSqlLatestVersionMatchesLatestFile pg_documentdb
CheckSqlLatestVersionMatchesLatestFile internal/pg_documentdb_distributed
CheckSqlLatestVersionMatchesLatestFile pg_documentdb_gw

# Ensure no header collisions
$repoScriptDir/validate_headers.sh
#!/bin/bash

# exit immediately if a command exits with a non-zero status
set -e
# fail if trying to reference a variable that is not set.
set -u

PG_VERSION=${PG_VERSION_USED:-16}
valgrindToggle=""
help=""
valgrind_suppressions_file=""
enable_valgrind_debugging="false"
pg_config_path="pg_config"
while getopts "dehs:xp:" opt; do
  case $opt in
    d) valgrindToggle="disable"
    ;;
    e) valgrindToggle="enable"
    ;;
    h) help="true"
    ;;
    s) valgrind_suppressions_file="$OPTARG"
    ;;
    x) enable_valgrind_debugging="true"
    ;;
    p) pg_config_path="$OPTARG"
    ;;
  esac

  # Assume empty string if it's unset since we cannot reference to
  # an unset variabled due to "set -u".
  case ${OPTARG:-""} in
    -*) echo "Option $opt needs a valid argument. use -h to get help."
    exit 1
    ;;
  esac
done

if [ "$valgrindToggle" != "enable" ] && [ "$valgrindToggle" != "disable" ]; then
  help="true"
fi

if [ "$help" == "true" ]; then
    echo "Run "
    echo "    set_valgrind_on_postgres.sh -e to enable valgrind"
    echo "    set_valgrind_on_postgres.sh -d to disable valgrind"
    echo "   specify custom suppressions file with -s <path>"
    exit 1;
fi

source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

pg_bin_directory=$($pg_config_path --bindir)
postgres_host=$pg_bin_directory/postgres
postgres_orig_host=$pg_bin_directory/postgres.orig
postgres_script_file=$pg_bin_directory/postgres.valgrind

if [ "$valgrind_suppressions_file" == "" ]; then
  versionStr=$($pg_config_path --version | awk -F' ' '{ print $2 }' | awk -F'.' '{ print $1 }')
  mkdir -p $scriptDir/build/$versionStr
  valgrind_suppressions_file="$scriptDir/build/$versionStr/valgrind.supp"
  if [ ! -f $valgrind_suppressions_file ]; then
    echo "Downloading valgrind suppressions file for $versionStr"
    curl -o $valgrind_suppressions_file -JLO https://raw.githubusercontent.com/postgres/postgres/refs/heads/REL_${versionStr}_STABLE/src/tools/valgrind.supp
  fi
fi

# Save the original host file
if [ ! -f $postgres_orig_host ]; then
cp -n $postgres_host $postgres_orig_host
fi

# now that everything is set up - check what the user wants
if [ "$valgrindToggle" == "enable" ]; then
valgrind_debug_flags=""
if [ "$enable_valgrind_debugging" == "true" ]; then
  valgrind_debug_flags="--vgdb-error=1 --vgdb=yes"
fi
  
cat << EOF > $postgres_script_file
#!/bin/bash
valgrind --quiet --exit-on-first-error=yes $valgrind_debug_flags --error-markers=VALGRINDERROR-BEGIN,VALGRINDERROR-END \
  --error-exitcode=1 --read-var-info=no --leak-check=no --max-stackframe=16000000 --time-stamp=yes --track-origins=yes --gen-suppressions=all \
  --trace-children=yes --suppressions=$valgrind_suppressions_file $postgres_orig_host \$@
EOF
  chmod 755 $postgres_script_file

  cp $postgres_script_file $postgres_host
elif [ "$valgrindToggle" == "disable" ]; then
  cp $postgres_orig_host $postgres_host
else
  echo "Invalid toggle specified";
  exit 1;
fi

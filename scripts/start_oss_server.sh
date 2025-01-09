#!/bin/bash

# exit immediately if a command exits with a non-zero status
set -e
# fail if trying to reference a variable that is not set.
set -u

coordinatorPort="9712"
postgresDirectory=""
initSetup="false"
help="false"
stop="false"
serverType="helio"
while getopts "d:t:hcs" opt; do
  case $opt in
    d) postgresDirectory="$OPTARG"
    ;;
    t) serverType="$OPTARG"
    ;;
    c) initSetup="true"
    ;;
    h) help="true"
    ;;
    s) stop="true"
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

red=`tput setaf 1`
green=`tput setaf 2`
reset=`tput sgr0`

if [ "$help" == "true" ]; then
    echo "${green}sets up and launches a postgres server with extension installed on port $coordinatorPort."
    echo "${green}start_oss_server -d <postgresDir> [-t <serverType>] [-c] [-s]"
    echo "${green}<postgresDir> is the data directory for your postgres instance with extension"
    echo "${green}[-t] - optional argument. serverType is the type of server to start. Supported values are helio and documentdb, default is helio"
    echo "${green}[-c] - optional argument. removes all existing data if it exists"
    echo "${green}[-s] - optional argument. Stops all servers and exits"
    echo "${green}if postgresDir not specified assumed to be ~/${serverType}_test"
    exit 1;
fi

extensionName=""
preloadLibraries=""
if [ "$serverType" == "helio" ]; then
  extensionName="pg_helio_api"
  preloadLibraries="pg_helio_core, pg_helio_api"
elif [ "$serverType" == "documentdb" ]; then
  extensionName="documentdb"
  preloadLibraries="pg_documentdb_core, pg_documentdb"
else
  echo "${red}Unknown server type ${serverType}"
  exit 1
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

. $scriptDir/utils.sh

if [ -z $postgresDirectory ]; then
    postgresDirectory="$HOME/${serverType}_test"
fi

if ! [ -d "$postgresDirectory" ]; then
    initSetup="true"
fi

# We stop the coordinator first and the worker node servers
# afterwards. However this order is not required and it doesn't
# really matter which order we choose to stop the active servers.
echo "${green}Stopping any existing postgres servers${reset}"
StopServer $postgresDirectory

if [ "$stop" == "true" ]; then
  exit 0;
fi

if [ "$initSetup" == "true" ]; then
    InitDatabaseExtended $postgresDirectory "$preloadLibraries"
fi

userName=$(whoami)
sudo mkdir -p /var/run/postgresql
sudo chown -R $userName:$userName /var/run/postgresql

StartServer $postgresDirectory $coordinatorPort

if [ "$initSetup" == "true" ]; then
  SetupPostgresServerExtensions "$userName" $coordinatorPort $extensionName
fi

. $scriptDir/setup_psqlrc.sh
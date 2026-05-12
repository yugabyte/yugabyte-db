#!/bin/bash

# exit immediately if a command exits with a non-zero status
set -e
# fail if trying to reference a variable that is not set.
set -u

PG_VERSION=16

# Overrides from environment
if [ "${PG_VERSION_USED:-}" != "" ]; then
  PG_VERSION=$PG_VERSION_USED
elif [ "${PGVERSION_USED:-}" != "" ]; then
  PG_VERSION=${PGVERSION_USED}
fi


coordinatorPort="9712"
postgresDirectory=""
initSetup="false"
forceCleanup="false"
help="false"
stop="false"
distributed="false"
allowExternalAccess="false"
gatewayWorker="false"
useDocumentdbExtendedRum="false"
customAdminUser="docdb_admin"
customAdminUserPassword="Admin100"
valgrindMode="false"
extraConfigFile=""
logPath=""
while getopts "d:p:u:a:hcsxegrvf:l:" opt; do
  case $opt in
    d) postgresDirectory="$OPTARG"
    ;;
    c) initSetup="true"
       forceCleanup="true"
    ;;
    h) help="true"
    ;;
    s) stop="true"
    ;;
    x) distributed="true"
    ;;    
    e) allowExternalAccess="true"
    ;;
    p) coordinatorPort="$OPTARG"
    ;;
    g) gatewayWorker="true"
    ;;
    r) useDocumentdbExtendedRum="true"
    ;;
    u) customAdminUser="$OPTARG"
    ;;
    a) customAdminUserPassword="$OPTARG"
    ;;
    v) valgrindMode="true"
    ;;
    f) extraConfigFile="$OPTARG"
    ;;
    l) logPath="$OPTARG"
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

if [ "${TERM:-}" == "" ] || [ "${TERM:-}" == "dumb" ]; then
  red=""
  green=""
  reset=""
else
  red=`tput setaf 1`
  green=`tput setaf 2`
  reset=`tput sgr0`
fi

if [ "$help" == "true" ]; then
    echo "${green}sets up and launches a postgres server with extension installed on port $coordinatorPort."
    echo "${green}start_oss_server -d <postgresDir> [-c] [-s] [-x] [-e] [-p <port>]"
    echo "${green}<postgresDir> is the data directory for your postgres instance with extension"
    echo "${green}[-c] - optional argument. FORCE cleanup - removes all existing data and reinitializes"
    echo "${green}[-s] - optional argument. Stops all servers and exits"
    echo "${green}[-x] - start oss server with documentdb_distributed extension"
    echo "${green}[-e] - optional argument. Allows PostgreSQL access from any IP address"
    echo "${green}[-p <port>] - optional argument. specifies the port for the backend"
    echo "${green}[-u <user>] - optional argument. Specifies a custom admin user to connect to the database"
    echo "${green}[-a <password>] - optional argument. Specifies the password for the custom admin user"
    echo "${green}[-g] - optional argument. starts the gateway worker host along with the backend"
    echo "${green}[-r] - optional argument. use the pg_documentdb_extended_rum extension instead of rum"
    echo "${green}[-v] - optional argument. run via valgrind mode"
    echo "${green}[-f <file>] - optional argument. add this extra conf file to postgresql.conf"
    echo "${green}[-l <file>] - optional argument. log to this file for the postgres server logs"
    echo "${green}if postgresDir not specified assumed to be $HOME/.documentdb/data"
    exit 1;
fi

if ! [[ "$coordinatorPort" =~ ^[0-9]+$ ]] || [ "$coordinatorPort" -lt 0 ] || [ "$coordinatorPort" -gt 65535 ]; then
    echo "${red}Invalid port value $coordinatorPort, must be a number between 0 and 65535.${reset}"
    exit 1
fi

# Check if the port is already in use (skip if stopping)
if [ "$stop" != "true" ] && lsof -i:"$coordinatorPort" -sTCP:LISTEN >/dev/null 2>&1; then
  echo "${red}Port $coordinatorPort is already in use. Please specify a different port.${reset}"
  exit 1
fi

if [ "$distributed" == "true" ]; then
  extensionName="documentdb_distributed"
else
  extensionName="documentdb"
fi

preloadLibraries="pg_documentdb_core, pg_documentdb"

if [ "$distributed" == "true" ]; then
  preloadLibraries="citus, $preloadLibraries, pg_documentdb_distributed"
fi

if [ "$gatewayWorker" == "true" ]; then
  preloadLibraries="$preloadLibraries, pg_documentdb_gw_host"
fi

if [ "$useDocumentdbExtendedRum" == "true" ]; then
  preloadLibraries="$preloadLibraries, pg_documentdb_extended_rum"
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
    postgresDirectory="$HOME/.documentdb/data"
fi

# Only initialize if directory doesn't exist, is empty, or doesn't contain a valid PostgreSQL data directory
# Check for PG_VERSION file which indicates a valid PostgreSQL data directory
if [ ! -d "$postgresDirectory" ]; then
    # Directory doesn't exist, we need to initialize
    echo "${green}Directory $postgresDirectory doesn't exist, will initialize PostgreSQL data directory${reset}"
    initSetup="true"
elif [ ! -f "$postgresDirectory/PG_VERSION" ]; then
    # Directory exists but no PG_VERSION file
    if [ "$(ls -A "$postgresDirectory" 2>/dev/null)" ]; then
        # Directory exists and is not empty but doesn't have PG_VERSION
        # This might be a corrupted or incompatible data directory
        echo "${red}Warning: Directory $postgresDirectory exists but doesn't appear to contain a valid PostgreSQL data directory.${reset}"
        echo "${red}Use -c flag to force cleanup and re-initialization, or specify a different directory with -d.${reset}"
        exit 1
    else
        # Directory exists but is empty, we can initialize
        echo "${green}Directory $postgresDirectory is empty, will initialize PostgreSQL data directory${reset}"
        initSetup="true"
    fi
else
    # Directory exists and has PG_VERSION, check if it's compatible
    echo "${green}Found existing PostgreSQL data directory at $postgresDirectory${reset}"
fi

# We stop the coordinator first and the worker node servers
# afterwards. However this order is not required and it doesn't
# really matter which order we choose to stop the active servers.
echo "${green}Stopping any existing postgres servers${reset}"
StopServer $postgresDirectory

pg_config_path=$(GetPGConfig $PG_VERSION)

if [ "$valgrindMode" == "true" ]; then
  # Disable valgrind on shutdown.
  echo "Disabling valgrind on server with pg_config $pg_config_path"
  sudo $scriptDir/set_valgrind_on_postgres.sh -d -p $pg_config_path
fi

if [ "$stop" == "true" ]; then
  exit 0;
fi

echo "InitDatabaseExtended $initSetup $postgresDirectory"

if [ "$initSetup" == "true" ]; then
    InitDatabaseExtended $postgresDirectory "$preloadLibraries"
fi

# Update PostgreSQL configuration to allow access from any IP
postgresConfigFile="$postgresDirectory/postgresql.conf"
if [ "$allowExternalAccess" == "true" ]; then
  hbaConfigFile="$postgresDirectory/pg_hba.conf"

  echo "${green}Configuring PostgreSQL to allow access from any IP address${reset}"
  echo "listen_addresses = '*'" >> $postgresConfigFile
  echo "host all all 0.0.0.0/0 scram-sha-256" >> $hbaConfigFile
  echo "host all all ::0/0 scram-sha-256" >> $hbaConfigFile
fi

if [ "$gatewayWorker" == "true" ]; then
  setupConfigurationFile="$scriptDir/../pg_documentdb_gw/SetupConfiguration.json"
  echo "documentdb_gateway.database = 'postgres'" >> $postgresConfigFile
  echo "documentdb_gateway.setup_configuration_file = '$setupConfigurationFile'" >> $postgresConfigFile
fi

if [ "$useDocumentdbExtendedRum" == "true" ] && [ "$initSetup" == "true" ]; then
  echo "${green}Configuring PostgreSQL to use pg_documentdb_extended_rum extension instead of rum${reset}"
  echo "documentdb.rum_library_load_option = 'require_documentdb_extended_rum'" >> $postgresConfigFile
  echo "documentdb.alternate_index_handler_name = 'extended_rum'" >> $postgresConfigFile
fi

if [ "$extraConfigFile" != "" ]; then
  echo "include '$extraConfigFile'" >> $postgresConfigFile
fi

userName=$(whoami)
if [ ! -d /var/run/postgresql ]; then
  sudo mkdir -p /var/run/postgresql
  sudo chown -R $userName:$userName /var/run/postgresql
fi

if [ "$logPath" == "" ]; then
  logPath="$postgresDirectory/pglog.log"
fi

StartServer $postgresDirectory $coordinatorPort $logPath

if [ "$initSetup" == "true" ]; then
  SetupPostgresServerExtensions "$userName" $coordinatorPort $extensionName

  if [ "$useDocumentdbExtendedRum" == "true" ] && [ "$initSetup" == "true" ]; then
    psql -p $coordinatorPort -d postgres -c "CREATE EXTENSION documentdb_extended_rum"
  fi

  if [ "$distributed" == "true" ]; then
    psql -p $coordinatorPort -d postgres -c "SELECT citus_set_coordinator_host('localhost', $coordinatorPort);"
    AddNodeToCluster $coordinatorPort $coordinatorPort
    psql -p $coordinatorPort -d postgres -c "SELECT documentdb_api_distributed.initialize_cluster()"
  fi
  if [ "$customAdminUser" != "" ]; then
    SetupCustomAdminUser "$customAdminUser" "$customAdminUserPassword" $coordinatorPort "$userName"
  fi
fi

if [ "$valgrindMode" == "true" ]; then
  # Ensure that initdb is not run via valgrind
  StopServer $postgresDirectory
  echo "Enabling valgrind on server"
  if [ "${ENABLE_VALGRIND_DEBUGGING:-}" == "1" ]; then
    sudo $scriptDir/set_valgrind_on_postgres.sh -e -x -p $pg_config_path
  else
    sudo $scriptDir/set_valgrind_on_postgres.sh -e -p $pg_config_path
  fi
  StartServer $postgresDirectory $coordinatorPort $logPath "-W"

  # Now wait for server up via pgctl
  echo -n "Waiting for server up with valgrind via pg_ctl"
  wait="true"
  while [ "$wait" == "true" ]; do
    result=$($(GetPGCTL) status -D $postgresDirectory || true)
    if [[ "$result" =~ "server is running" ]]; then
      echo "Server is up via pg_ctl."
      wait="false";
    else
      echo -n "."
      sleep 1;
    fi
  done

  # Validate connectivity via psql as well
  echo -n "Waiting for server up with valgrind via psql"
  wait="true"
  while [ "$wait" == "true" ]; do
    result=$(psql -X -t -d postgres -p $coordinatorPort -c "SELECT 1" || true)
    if [[ "$result" =~ "1" ]]; then
      echo "Server is up via psql."
      wait="false";
    else
      echo -n "."
      sleep 1;
    fi
  done
fi

. $scriptDir/setup_psqlrc.sh
#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# trap to print line number on error
error() {
  local parent_lineno="$1"
  local message="${2:-x}"
  local code="${3:-1}"
  if [[ -n "$message" ]] ; then
    echo "Error on or near line ${parent_lineno}: ${message}; exiting with status ${code}"
  else
    echo "Error on or near line ${parent_lineno}; exiting with status ${code}"
  fi
  exit "${code}"
}
trap 'error ${LINENO}' ERR

function GetPostgresPath()
{
  local pgVersion=$1
  local osVersion=$(cat /etc/os-release | grep "^ID=");

  if [[ "$osVersion" == "ID=ubuntu" || "$osVersion" == "ID=mariner" ]]; then
    echo "/usr/lib/postgresql/$pgVersion/bin"
  else
    echo "/usr/pgsql-$pgVersion/bin"
  fi
}

function StopServer()
{
  local _directory=$1
  local _extraOptions=${2:-""}
  local _pgctlPath=${pgctlPath:-pg_ctl}

  $_pgctlPath stop -D $_directory $_extraOptions || true;
  echo "Stopped all PG instances on $_directory";
}

function StartServer()
{
  local _directory=$1
  local _port=$2
  local _logPath=${3:-$_directory/pglog.log}
  local _pgctlPath=${pgctlPath:-pg_ctl}

  echo "Starting postgres in $_directory"
  echo "Calling: pg_ctl start -D $_directory -o \"-p $_port -l $_logPath\""
  $_pgctlPath start -D $_directory -o "-p $_port" -l $_logPath
}


function SetupPostgresServerExtensions()
{
  local user=$1
  local port=$2
  local extensionName=$3
  local extensionVersion=${4:-}
  
  local versionString="";
  if [ "$extensionVersion" != "" ]; then
    versionString="WITH VERSION '${extensionVersion}'";
  fi

  echo "create extension $extensionName on port $port with version '${extensionVersion:-latest}'."
  psql -p $port -U $user -d postgres -c "CREATE EXTENSION $extensionName $versionString CASCADE;"

  psql -p $port -U $user -d postgres -c "SELECT * FROM pg_extension WHERE extname = '$extensionName';"
}


function InitDatabaseExtended()
{
  local _directory=$1
  
  echo "Deleting directory $_directory"
  rm -rf $_directory
  mkdir $_directory

  echo "Calling initdb for $_directory"
  initdb -D $_directory
  SetupPostgresConfigurations $_directory
}


function SetupPostgresConfigurations()
{
  local installdir=$1;
  requiredExtensions="pg_cron,pg_helio_core,pg_helio_api";
  echo shared_preload_libraries = \'$requiredExtensions\' | tee -a $installdir/postgresql.conf
  echo cron.database_name = \'postgres\' | tee -a $installdir/postgresql.conf
  echo ssl = off | tee -a $installdir/postgresql.conf
}

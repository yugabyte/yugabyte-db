#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# declare all the versions of dependencies
LIBBSON_VERSION=1.28.0
# This maps to REL_16_2:b78fa8547d02fc72ace679fb4d5289dccdbfc781
POSTGRES_16_REF="REL_16_2"
# This maps to REL15_3:8382864eb5c9f9ebe962ac20b3392be5ae304d23
POSTGRES_15_REF="REL_15_3"
# This is commit c44682a7d0641748c7fb3427fdb90ea2ae465a47
CITUS_VERSION=v12.1.6
# This is commit e6facb10caa1fb41faa8139f2116c282a6dfdde9
RUM_VERSION=1.3.13
# This is commit 9d0576c64edd90fb3d8ac30763296a8106315638
PG_CRON_VERSION=v1.6.3
# This is commit 2627c5ff775ae6d7aef0c430121ccf857842d2f2
PGVECTOR_VERSION=v0.8.0

POSTGIS_VERSION=3.4.3
INTEL_DECIMAL_MATH_LIB_VERSION=20U2
PCRE2_VERSION=10.40

function GetPostgresSourceRef()
{
  local pgVersion=$1
  if [ "$pgVersion" == "16" ]; then
    echo $POSTGRES_16_REF
  elif [ "$pgVersion" == "15" ]; then
    echo $POSTGRES_15_REF
  else
    echo "Invalid PG Version specified $pgVersion";
    exit 1;
  fi
}

function GetCitusVersion()
{
  local citusVersion=$1
  # allow the caller to specify the version as 12 or v12.1 or v12.1.6
  if [ "$citusVersion" == "12" ] || [ "$citusVersion" == "v12.1" ] || [ "$citusVersion" == "$CITUS_VERSION" ]; then
    echo $CITUS_VERSION
  else
    echo "Invalid Citus version specified $citusVersion. Please use $CITUS_VERSION'."
    exit 1  
  fi
}

function GetRumVersion()
{
  echo $RUM_VERSION
}

function GetLibbsonVersion()
{
  echo $LIBBSON_VERSION
}

function GetPgCronVersion()
{
  echo $PG_CRON_VERSION
}

function GetPgVectorVersion()
{
  echo $PGVECTOR_VERSION
}

function GetIntelDecimalMathLibVersion()
{
  echo $INTEL_DECIMAL_MATH_LIB_VERSION
}

function GetPcre2Version()
{
  echo $PCRE2_VERSION
}

function GetPostgisVersion()
{
  echo $POSTGIS_VERSION
}

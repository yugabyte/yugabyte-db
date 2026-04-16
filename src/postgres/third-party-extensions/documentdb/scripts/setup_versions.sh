#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# declare all the versions of dependencies
LIBBSON_VERSION=1.28.0
# This maps to REL_17_4:f8554dee417ffc4540c94cf357f7bf7d4b6e5d80
POSTGRES_17_REF="REL_17_4"
# This maps to REL_16_8:71eb35c0b18de96537bd3876ec9bf8075bfd484f
POSTGRES_16_REF="REL_16_8"
# This maps to REL15_12:50d3d22baba63613d1f1406b2ed460dc9b03c3fc
POSTGRES_15_REF="REL_15_12"
# This is commit c44682a7d0641748c7fb3427fdb90ea2ae465a47
CITUS_VERSION=v12.1.6
# This is commit d28a5eae6c78935313824d319480632783d48d10
CITUS_13_VERSION=v13.0.1
# This is commit 6a065fd8dfb280680304991aa30d7f72787fdb04
RUM_VERSION=1.3.14
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
  if [ "$pgVersion" == "17" ]; then
    echo $POSTGRES_17_REF
  elif [ "$pgVersion" == "16" ]; then
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
  if [ "$PGVERSION" == "17" ]; then
    echo $CITUS_13_VERSION
  elif [ "$citusVersion" == "13" ] || [ "$citusVersion" == "v13.0" ] || [ "$citusVersion" == "$CITUS_13_VERSION" ]; then
    echo $CITUS_13_VERSION
  # allow the caller to specify the version as 12 or v12.1 or v12.1.6
  elif [ "$citusVersion" == "12" ] || [ "$citusVersion" == "v12.1" ] || [ "$citusVersion" == "$CITUS_VERSION" ]; then
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

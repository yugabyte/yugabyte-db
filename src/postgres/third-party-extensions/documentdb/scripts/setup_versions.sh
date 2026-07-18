#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# declare all the versions of dependencies
LIBBSON_VERSION=1.28.0

# This maps to REL_18_0:3d6a828938a5fa0444275d3d2f67b64ec3199eb7
POSTGRES_18_REF="REL_18_0"

# This maps to REL_17_6:7885b94dd81b98bbab9ed878680d156df7bf857f
POSTGRES_17_REF="REL_17_6"

# This maps to REL_16_10:c13dd7d50f21268dc64b4b3edbce31993985ab12
POSTGRES_16_REF="REL_16_10"

# This maps to REL_15_14:0ab43b548237b3791261480d6a023f6b95b53942
POSTGRES_15_REF="REL_15_14"

# this is commit c569f8321f4cbd431f8fa36417df4a3ae025a417
# This contains the fix for crashes for ASAN on version handling.
CITUS_VERSION=c569f8321f4cbd431f8fa36417df4a3ae025a417
# This is commit 9aa1384d9d0fcecc326b6c48919d774ad8389e0f
CITUS_13_VERSION=9aa1384d9d0fcecc326b6c48919d774ad8389e0f
# For pg18 use an unstable main version for now
CITUS_PG18_VERSION=b7bfe42f1a4d22db4b1ecc2636cdf83adf27c106
# This is commit 6a065fd8dfb280680304991aa30d7f72787fdb04
RUM_VERSION=1.3.14
# This is commit 465b38c737f584d520229f5a1d69d1d44649e4e5
PG_CRON_VERSION=v1.6.7
# This is commit 778dacf20c07caf904557a88705142631818d8cb
PGVECTOR_VERSION=v0.8.1

POSTGIS_VERSION=3.6.0
INTEL_DECIMAL_MATH_LIB_VERSION=applied/2.0u3-1
PCRE2_VERSION=10.40
UNCRUSTIFY_VERSION=uncrustify-0.68.1

function GetPostgresSourceRef()
{
  local pgVersion=$1
  if [ "$pgVersion" == "18" ]; then
    echo $POSTGRES_18_REF
  elif [ "$pgVersion" == "17" ]; then
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
  if [ "$PGVERSION" == "18" ]; then
    echo $CITUS_PG18_VERSION
  elif [ "$PGVERSION" == "17" ]; then
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

function GetUncrustifyVersion()
{
  echo $UNCRUSTIFY_VERSION
}
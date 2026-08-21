#!/bin/bash
set -euo pipefail

function print_usage {
  echo "Usage: ./set-runtime-config.sh <yw_url> <api_token> <key> <value> <optional_Scope>"
  exit 1
}

if [ $# -lt 4 ]; then
  echo "Insufficient arguments"
  print_usage
fi

if [ "$1" == "--help" ]; then
  print_usage
fi

YW_URL=$1
TOK=$2
KEY=$3
VALUE=$4
SCOPE=${5:-00000000-0000-0000-0000-000000000000}

echo "==================================================
 YW_URL=$YW_URL
 TOK=$TOK
 KEY=$KEY
 VALUE=$VALUE
 SCOPE=$SCOPE
=================================================="

CUUID=`curl --request GET   --url $YW_URL/api/v1/session_info  --header "X-AUTH-YW-API-TOKEN: $TOK" | cut -f8 -d\"`
echo "customerUUID: $CUUID"

echo "Fetching Current value of $SCOPE/$KEY ..."
curl --request GET   --url $YW_URL/api/v1/customers/$CUUID/runtime_config/$SCOPE/key/$KEY --header "X-AUTH-YW-API-TOKEN: $TOK"
echo

echo "Setting $SCOPE/$KEY value to $VALUE"
curl --request PUT   --url $YW_URL/api/v1/customers/$CUUID/runtime_config/$SCOPE/key/$KEY --header "X-AUTH-YW-API-TOKEN: $TOK"  --header "Content-Type: text/plain" --data "$VALUE"
echo

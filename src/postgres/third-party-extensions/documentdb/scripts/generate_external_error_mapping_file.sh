#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

documentdbSourceFile=$1
codeMappingFile=$2
targetFile=$3

declare -A documentdbErrorKeys=()
declare -A documentdbErrorOrdinals=()
isFirst=""
maxOrdinal=0
for fileLine in $(cat $documentdbSourceFile); do
        # Skip the header.
    if [ "$isFirst" = "" ]; then
        isFirst="false"
        continue;
    fi

    # Format is error,pgcode,ordinal
    regex="([A-Za-z0-9]+),([A-Za-z0-9]+),([0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        _name="${BASH_REMATCH[1]}"
        _pgError="${BASH_REMATCH[2]}"
        _existOrdinal="${BASH_REMATCH[3]}"

        documentdbErrorKeys["$_name"]=$_pgError
        documentdbErrorOrdinals["$_existOrdinal"]=$_name

        if (( $maxOrdinal < $_existOrdinal )); then
            maxOrdinal=$_existOrdinal
        fi
    else
        echo "ERROR: documentdb error file line has unknown format $fileLine"
        exit 1
    fi
done

declare -A externalErrorKeys=()
isFirst=""
_maxErrorCode=0
for fileLine in $(cat $codeMappingFile); do
        # Skip the header.
    if [ "$isFirst" = "" ]; then
        isFirst="false"
        continue;
    fi

    # Format is ExternalError,ErrorName
    regex="([0-9]+),([A-Za-z0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        _externalError="${BASH_REMATCH[1]}"
        _externalErrorName="${BASH_REMATCH[2]}"

        externalErrorKeys["$_externalErrorName"]=$_externalError

        if (( $_maxErrorCode < $_externalError )); then
            _maxErrorCode=$_externalError
        else
            echo "externalErrors must be in ascending order: detected $_externalError with max $_maxErrorCode"
            exit 1
        fi
    else
        echo "ERROR: external file line has unknown format $fileLine"
        exit 1
    fi
done

echo "Max error code is $_maxErrorCode, maxOrdinal is $maxOrdinal"
echo "external error count ${#externalErrorKeys[@]}, documentdb error count ${#documentdbErrorKeys[@]}"

if [ "${#externalErrorKeys[@]}" != "${#documentdbErrorKeys[@]}" ]; then
    echo "mismatch between documentdb errors and external errors detected";
    exit 1
fi

echo "ErrorName,ErrorCode,ExternalError,ErrorOrdinal" > $targetFile
for fileIndex in $(seq 1 $maxOrdinal); do
    _errorName=${documentdbErrorOrdinals[$fileIndex]}
    _errorCode=${documentdbErrorKeys[$_errorName]}
    _externalError=${externalErrorKeys[$_errorName]}

    echo "$_errorName,$_errorCode,$_externalError,$fileIndex" >> $targetFile
done
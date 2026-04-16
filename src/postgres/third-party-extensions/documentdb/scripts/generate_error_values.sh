#!/bin/bash

#########################################################
# This script generates the error code mappings for
# DocumentDB API/DocumentDB Core.
# As an input takes the base error csv file that is expected
# to be of the form 
#           ErrorName,OrdinalPosition
# Where ErrorName is the user friendly name
# And Ordinal position is a monotonically increasing number
# that is unique across error codes.
# Generates a header file with the error codes for C code to
# use and a generated csv file that has the form
#           ErrorName,ErrorCode,OrdinalPosition
# Which has the User friendly name above, the PG error string
#########################################################

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# Grab the input args.
sourceFile=$1
filePathDest=$2
errorNamesPathDest=$3
extensionObjectName=$4

# Get a temp path to write to for staging
filePathName=$(basename $filePathDest)
filePathName="${RANDOM}-${filePathName}"

errNamesFileName=$(basename $errorNamesPathDest)
errNamesFileName="${RANDOM}-${errNamesFileName}"

filePath="/tmp/$filePathName"
errorNamesPath="/tmp/$errNamesFileName"

function WriteHeaderBase()
{
  local _filePath=$1;
  local _fileName=$2;
  local _headerDef=$3
  local _sourceFileName=$(basename $sourceFile)

  echo "Writing header for $_filePath"
cat << EOF > $_filePath

/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/$_fileName
 *
 * Utilities for $extensionObjectName Error Definition.
 * This file is generated - please modify the source ($_sourceFileName)
 *
 *-------------------------------------------------------------------------
 */

#ifndef $_headerDef
#define $_headerDef

#include <utils/elog.h>

EOF
}

extensionObjectNameUpper=${extensionObjectName^^}
extensionObjectNameLower=${extensionObjectName,,}
extensionObjectNameTitle="${extensionObjectName[@]^}"

echo "Writing initial content for $filePath"
WriteHeaderBase $filePath "${extensionObjectName}_errors.h" ${extensionObjectNameUpper}_ERRORS_H
cat << EOF >> $filePath
/* Represents a $extensionObjectNameTitle error */
typedef int ${extensionObjectNameTitle}ErrorEreportCode;
EOF

# Write the CSV header file.
echo "Writing header for $errorNamesPath"
echo "ErrorName,ErrorCode,ErrorOrdinal" > $errorNamesPath

# Postgres Printable Error Range - used in error mode calculation
ValidChars=(0 1 2 3 4 5 6 7 8 9 A B C D E F G H I J K L M N O P Q R S T U V W X Y Z)

isFirst=""

# The error codes for the extension start at "M0000"
baseLetter="M"
baseSecond="0"
baseThird="0"
baseFourth="0"
baseFifth="0"

# helper function to increment a character in the ValidChars range.
# If over the range, returns -1.
function IncrementCharacter()
{
    local _character=$1
    if [[ $_character -lt 35 ]]; then
        _character=$((++_character))
        echo $_character
    else
        echo "-1"
    fi
}

# Increments the 5 character error code. On rollover, increments the
# next character.
function IncrementErrorCode()
{
    newFifth=$(IncrementCharacter $baseFifth)

    if [[ "$newFifth" == "-1" ]]; then
        baseFifth="0"

        newFourth=$(IncrementCharacter $baseFourth)
        if [[ "$newFourth" == "" ]]; then
            baseFourth="0"

            newThird=$(IncrementCharacter $baseThird)
            if [[ "$newThird" == "" ]]; then
                baseThird="0"

                newSecond=$(IncrementCharacter $baseSecond)
                if [[ "$newSecond" == "" ]]; then
                    echo "ERROR: Max error is reached."
                    exit 1
                else
                    baseSecond=$newSecond
                    return;
                fi
            else
                baseThird=$newThird;
                return;
            fi

        else
            baseFourth=$newFourth
            return;
        fi
    else
        baseFifth=$newFifth
        return;
    fi
}

declare -A myKeys=()
declare -A existingKeys=()
maxIndex=0
_maxCode=0
isFirst=""

# collect the error names and ordinals for existing errors
if [ -f $errorNamesPathDest ]; then

    for fileLine in $(cat $errorNamesPathDest); do
         # Skip the header.
        if [ "$isFirst" = "" ]; then
            isFirst="false"
            continue;
        fi

        # Format is error,pgcode,ordinal
        regex="([A-Za-z0-9]+),([A-Za-z0-9]+),([0-9]+)"
        if [[ $fileLine =~ $regex ]]; then
            _name="${BASH_REMATCH[1]}"
            _existOrdinal="${BASH_REMATCH[3]}"

            existingKeys["$_name"]=$_existOrdinal
        else
            echo "ERROR: Target file line has unknown format $fileLine"
            exit 1
        fi
    done
fi

isFirst=""

# Read through the file and get the error codes
for fileLine in $(cat $sourceFile); do
    # Skip the header.
    if [ "$isFirst" = "" ]; then
        isFirst="false"
        continue;
    fi

    # Parse the CSV line
    regex="([0-9]+),([A-Za-z0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        _name="${BASH_REMATCH[2]}"
        _ordinal="${BASH_REMATCH[1]}"
        lineToEnter="${_name}"

        # Ensure uniqueness of ordinal values.
        if [[ ${myKeys["$_ordinal"]:-''} != '' ]]; then
            echo "ERROR: Ordinal Used Already $_ordinal"
            exit 1;
        fi

        if [[ ${existingKeys["$_name"]:-''} != '' ]] && [[ ${existingKeys["$_name"]:-''} != "$_ordinal" ]]; then
            echo "ERROR: Cannot change ordinal number for existing error $_name from  ${existingKeys["$_name"]} to $_ordinal"
            exit 1
        fi

        myKeys["$_ordinal"]=$lineToEnter

        # Track in ascending ordinal order
        # Track the highest ordinal.
        if (( $maxIndex < $_ordinal )); then
            maxIndex=$_ordinal
        else
            echo "ErrorOrdinal must be in ascending order: detected $_ordinal with max $maxIndex"
            exit 1
        fi
    else
        echo "$fileLine doesn't match";
        exit 1
    fi
done

echo "Max ordinal found ${maxIndex}"

# Iterate through each ordinal.
for fileIndex in $(seq 1 $maxIndex); do

    if [[ ${myKeys["$fileIndex"]:-''} == '' ]]; then
        echo "ERROR: Ordinal $fileIndex does not have a valid error code"
        exit 1
    fi

    # Grab the error and rematch the regex.
    fileLine=${myKeys[$fileIndex]}
    regex="([A-Za-z0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        name="${BASH_REMATCH[1]}"
    else
        echo "ERROR: $fileLine doesn't match";
        exit 1
    fi

    # For the C macro use upper case for the macro name
    errorNameUpper=${name^^}

    # grab the printable char by index
    secondChar=${ValidChars[$baseSecond]}
    thirdChar=${ValidChars[$baseThird]}
    fourthChar=${ValidChars[$baseFourth]}
    fifthChar=${ValidChars[$baseFifth]}

    # Write the macro out.
    lineToWrite="#define ERRCODE_{$extensionObjectNameUpper}_$errorNameUpper MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"

    if (( ${#lineToWrite} > 89 )); then
        # make citus indent happy
        echo "#define ERRCODE_${extensionObjectNameUpper}_$errorNameUpper \\" >> $filePath
        echo "	MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"  >> $filePath
    else
        echo "#define ERRCODE_${extensionObjectNameUpper}_$errorNameUpper MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"  >> $filePath
    fi
    echo >> $filePath

    # Add the generated macro to the csv as well
    echo "${name},${baseLetter}${secondChar}${thirdChar}${fourthChar}${fifthChar},${fileIndex}" >> $errorNamesPath

    # Move to the next header.
    IncrementErrorCode
done

echo "Writing footer for $filePath"
echo "#endif" >> $filePath

echo "Moving $filePath to $filePathDest"
mv -f $filePath $filePathDest
mv -f $errorNamesPath $errorNamesPathDest
#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

sourceFile=$1
filePathDest=$2
errorNamesPath=$3

filePathName=$(basename $filePathDest)
filePathName="${RANDOM}-${filePathName}"
filePath="/tmp/$filePathName"

echo "Writing header for $filePath"
cat << EOF > $filePath

/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/helio_errors.h
 *
 * Utilities for Helio Error Definition.
 * This file is generated - please modify the source (mongoerrors.csv)
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_ERRORS_H
#define HELIO_ERRORS_H

#include <utils/elog.h>

EOF

echo "ErrorName,ErrorCode,MongoError" > $errorNamesPath

# Postgres Printable Error Range
ValidChars=(0 1 2 3 4 5 6 7 8 9 A B C D E F G H I J K L M N O P Q R S T U V W X Y Z)

isFirst=""
baseLetter="M"
baseSecond="0"
baseThird="0"
baseFourth="0"
baseFifth="0"

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
                    echo "Max error is reached."
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
declare -a myIndexes=()
isFirst=""

# Read through the file and get the error codes
for fileLine in $(cat $sourceFile); do
    if [ "$isFirst" = "" ]; then
        isFirst="false"
        continue;
    fi

    regex="([A-Za-z0-9]+),([0-9]+),([0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        _name="${BASH_REMATCH[1]}"
        _code="${BASH_REMATCH[2]}"
        _ordinal="${BASH_REMATCH[3]}"
        lineToEnter="${_name},${_code}"

        # Ensure uniqueness of ordinal values.
        if [[ ${myKeys["$_ordinal"]:-''} != '' ]]; then
            echo "Error Ordinal Used Already $_ordinal"
            exit 1;
        fi

        myKeys["$_ordinal"]=$lineToEnter
        myIndexes+=($_ordinal)
    else
        echo "$fileLine doesn't match";
        exit 1
    fi
done

# Sort by ordinal
sorted=($(sort <<<"${myIndexes[*]}"))

prevTrackedIndex=0
for fileIndex in $( echo ${sorted[@]} ); do

    prevOrdinal=$(( fileIndex - 1 ))

    if [[ "$prevOrdinal" != "$prevTrackedIndex" ]]; then
        echo "There's a gap in the ordinal index. This is not allowed: Index $fileIndex, Previous $prevTrackedIndex"
        exit 1;
    fi

    prevTrackedIndex=$fileIndex

    fileLine=${myKeys[$fileIndex]}
    regex="([A-Za-z0-9]+),([0-9]+)"
    if [[ $fileLine =~ $regex ]]; then
        name="${BASH_REMATCH[1]}"
        code="${BASH_REMATCH[2]}"
    else
        echo "$fileLine doesn't match";
        exit 1
    fi

    errorNameUpper=${name^^}
    secondChar=${ValidChars[$baseSecond]}
    thirdChar=${ValidChars[$baseThird]}
    fourthChar=${ValidChars[$baseFourth]}
    fifthChar=${ValidChars[$baseFifth]}

    lineToWrite="#define ERRCODE_HELIO_$errorNameUpper MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"

    if (( ${#lineToWrite} > 89 )); then
        # make citus indent happy
        echo "#define ERRCODE_HELIO_$errorNameUpper \\" >> $filePath
        echo "	MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"  >> $filePath
    else
        echo "#define ERRCODE_HELIO_$errorNameUpper MAKE_SQLSTATE('$baseLetter', '$secondChar', '$thirdChar', '$fourthChar', '$fifthChar')"  >> $filePath
    fi
    echo "#define MONGO_CODE_${errorNameUpper} $code"  >> $filePath
    echo >> $filePath

    echo "${name},${baseLetter}${secondChar}${thirdChar}${fourthChar}${fifthChar},${code}" >> $errorNamesPath

    IncrementErrorCode
done

echo "Writing footer for $filePath"
echo "#endif" >> $filePath

echo "Moving $filePath to $filePathDest"
mv $filePath $filePathDest
#!/bin/sh

# This script allows for passing args to psql using pg_regress's --launcher option

while [ $# -gt 0 ]; do
    case $1 in
        */psql)
            found=1
            break
            ;;
        *)
            args="$args $1"
            shift
            ;;
    esac
done

if [ -z "$found" ]; then
    echo "Error: psql not found in arguments"
    exit 1
fi

#$* $args <&0 || exit $?
$* $args || exit $?

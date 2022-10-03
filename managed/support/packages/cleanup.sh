#!/bin/bash
set -euo pipefail

read -p "Are you sure? Y/N: " -n 1 -r
echo

if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    exit 1
fi

echo "Stop services"
sudo service yb-platform stop
sudo service prometheus stop
sudo service nginx stop

echo "Drop platform database"
dropdb -U postgres yugaware

echo "Cleanup data directories"
rm -rf /opt/yugabyte


echo "Great you want to cleanup!!!"

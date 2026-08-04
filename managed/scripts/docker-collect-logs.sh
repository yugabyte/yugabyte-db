#!/bin/bash
set -euo pipefail

function print_usage {
  echo "Usage: ./docker-collect-logs.sh <universe-name>"
  echo "<universe-name> should be one of the following currently running universes:"
  # Print a unique list of universe names.
  for universe in `docker ps --format "{{ .Names }}" | awk -F- '{ print $2 }' | uniq`
  do
    echo "    - $universe"
  done
  exit 1
}

if [ $# -lt 1 ]; then
  echo "Please specify a valid universe name."
  print_usage
fi

if [ "$1" == "--help" ]; then
  print_usage
fi

UNIVERSE_NAME=$1
OUTPUT_DIR_NAME=$1
echo "Collecting logs for universe: $UNIVERSE_NAME"

# This is the output directory where the logs are stored.
DATE=$(date +"%y-%m-%d-%H%M%S")
OUTPUT_DIR_NAME=${OUTPUT_DIR_NAME}-logs-${DATE}
OUTPUT_DIR_PATH=/tmp/${OUTPUT_DIR_NAME}
echo "Creating output directory $OUTPUT_DIR_PATH..."
mkdir -p $OUTPUT_DIR_PATH

# Iterate through all the docker containers with the given cluster name.
echo "Collecting YB logs in $OUTPUT_DIR_PATH..."
for container in `docker ps --filter "name=$UNIVERSE_NAME" --format "{{ .Names }}"`
do
  echo "  - Fetching logs from container $container"
  docker cp $container:/var/log/yugabyte/master $OUTPUT_DIR_PATH/${container}-master
  docker cp $container:/var/log/yugabyte/tserver $OUTPUT_DIR_PATH/${container}-tserver
done

# Collect the yugaware logs.
echo "Collecting YW logs..."
cp ~/code/yugaware/logs/application.log $OUTPUT_DIR_PATH/

# Add some metadata about the universe.
echo "Preparing metadata file..."
cd ${OUTPUT_DIR_PATH}
METADATA_FILE_PATH=${OUTPUT_DIR_PATH}/metadata.txt
echo "  - Adding universe-name $UNIVERSE_NAME"
echo "universe-name ${UNIVERSE_NAME}" >> $METADATA_FILE_PATH
echo "  - Adding universe uuid"
ERROR_LOG_LINE=$(grep ${UNIVERSE_NAME} application.log | grep 'taskState: Failure' | tail -1)
UNIVERSE_UUID=$(echo $ERROR_LOG_LINE | sed 's/.*"universeUUID":"\([^"]*\)".*/\1/p' | head -1)
echo "universe-uuid $UNIVERSE_UUID" >> $METADATA_FILE_PATH

echo "Creating a tarball of collected logs at ${OUTPUT_DIR_PATH}.tar.gz..."
cd ${OUTPUT_DIR_PATH}/..
tar zcvf ${OUTPUT_DIR_NAME}.tar.gz ${OUTPUT_DIR_NAME}/*

echo "Deleting the log collection directory..."
rm -rf ${OUTPUT_DIR_PATH}

echo ""
echo "------------------------------------------------------"
echo "Output file: ${OUTPUT_DIR_PATH}.tar.gz"
echo "------------------------------------------------------"


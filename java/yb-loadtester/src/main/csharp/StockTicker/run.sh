#!/bin/sh
if [ $# -eq 0 ]
then
  echo "App not provided. Valid Apps:"
  cd code
  for app in *.exe
  do
    echo "$app"
  done
  exit
fi
mono code/$@ &
PID=$!
wait $PID

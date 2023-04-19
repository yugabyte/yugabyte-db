#!/bin/bash -x
#kill -9 $(ps aux | grep odyssey | grpe -v grep | awk '{print $2}')
sleep 1

#ody-start
./build/sources/odyssey ./odyssey-dev.conf

for _ in $(seq 1 40); do
  sleep 0.1

  for __ in $(seq 1 10); do
    psql -U postgres -d postgres -h 0.0.0.0 -p 6432 -c 'select pg_sleep(39)' &
    psql -U postgres -d postgres -h localhost -p 6432 -c 'select 1' &
    psql -U postgres -d postgres -h 0.0.0.0 -p 6432 -c 'select pg_sleep(1)' &
  done

  #ody-restart
  ps uax | grep odys
  ./build/sources/odyssey ./odyssey-dev.conf

  for __ in $(seq 1 30); do
    psql -U postgres -d postgres -h localhost -p 6432 -c 'select 1' &
    psql -U postgres -d postgres -h 0.0.0.0 -p 6432 -c 'select pg_sleep(39)' &
    psql -U postgres -d postgres -h 0.0.0.0 -p 6432 -c 'select pg_sleep(1)' &
  done

done

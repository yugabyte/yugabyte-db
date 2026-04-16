#!/bin/bash -x
#kill -9 $(ps aux | grep odyssey | grpe -v grep | awk '{print $2}')
sleep 1

#ody-start

for _ in $(eq 1 100); do
    psql -h localhost -p 6432 -c 'select 1' -U user1 -d postgres &
    psql -h 0.0.0.0 -p 6432 -c 'select pg_sleep(100)' -U user1 -d postgres &
done

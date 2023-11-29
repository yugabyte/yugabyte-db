#!/bin/bash


HOSTS="10.150.2.220:5433,10.150.3.45:5433,10.150.3.62:5433"
HOSTS="127.0.0.1:9042,127.0.0.2:9042"
HOSTS="127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042"
HOSTS="127.0.0.1:9042"
HOSTS="127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042"

HOSTS="172.151.19.211:9042" TABLE="cassandrakeyvalue" WORKLOAD="CassandraKeyValue"


HOSTS="127.0.0.1:5433,127.0.0.2:5433,127.0.0.3:5433" TABLE="postgreskeyvalue" WORKLOAD="SqlSecondaryIndex" OPTIONS=" "
HOSTS="127.0.0.1:5433"                               TABLE="postgreskeyvalue" WORKLOAD="SqlSecondaryIndex" OPTIONS=" "
HOSTS="127.0.0.1:9042,127.0.0.2:904,2127.0.0.3:9042" TABLE="cassandrakeyvalue" WORKLOAD="CassandraKeyValue"
HOSTS="127.0.0.1:9042" TABLE="cassandrakeyvalue" WORKLOAD="CassandraKeyValue"
HOSTS="127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042" WORKLOAD="CassandraBatchKeyValue"

HOSTS="172.151.24.129:9042,172.151.37.227:9042,172.152.24.183:9042" WORKLOAD="CassandraBatchKeyValue"

HOSTS="172.151.17.169:9042,172.152.100.71:9042,172.152.48.244:9042" WORKLOAD="CassandraBatchKeyValue"
HOSTS="127.0.0.1:9042" WORKLOAD="CassandraBatchKeyValue"

NUM_READERS="$1"
NUM_WRITERS="$2"
TAG="$3_r${NUM_READERS}w${NUM_WRITERS}"
RUN_OPTIONS=" --run_time 60 "

DATE=`date +'%m%d%y_%H%M'`
NONCE="${DATE}${TAG}"
FILE_PREFIX="follower_lag"

OPTIONS=" --run_time 60 --debug_driver true --cql_connect_timeout_ms 3000 --cql_read_timeout_ms 3000  --disable_yb_load_balancing_policy "

HOSTS="172.151.17.169:9042,172.152.100.71:9042,172.152.48.244:9042" WORKLOAD="CassandraBatchKeyValue"
OPTIONS="--batch_size 1000 --value_size 256 --username cassandra --password Test123$ "

HOSTS="172.151.17.169:5433,172.152.100.71:5433,172.152.48.244:5433" WORKLOAD="SqlSecondaryIndex"
OPTIONS="--truncate --batch_size 1000 --value_size 256 --username yugabyte --password Test123$ "
#--drop_table_name ${TABLE}
OPTIONS="--batch_size 1000 --value_size 256"

HOSTS="127.0.0.1:5433"                               TABLE="postgreskeyvalue" WORKLOAD="SqlSecondaryIndex" OPTIONS="--truncate "
HOSTS="127.0.0.1:9042" TABLE="cassandrakeyvalue" WORKLOAD="CassandraKeyValue"


#HOSTS="172.150.32.189:5433,172.156.26.137:5433" WORKLOAD="SqlSecondaryIndex"
#
#HOSTS="10.9.141.246:5433,10.9.203.59:5433,10.9.84.73:5433"
#OPTIONS="--truncate --batch_size 1000 --value_size 256 --username yugabyte --password p@ssword123"

NUM_READS=15000000 NUM_WRITES=75000000
NUM_READS=100000 NUM_WRITES=100000
NUM_READS=100000 NUM_WRITES=10000000000
NUM_READS=-1 NUM_WRITES=-1
set +e
JAR_DIR="~/code/yb-sample-apps/target"
JAR_DIR="../yb-sample-apps/target"
java -jar ${JAR_DIR}/yb-sample-apps.jar \
                       --workload $WORKLOAD $RUN_OPTIONS  \
                        --nodes $HOSTS $OPTIONS \
                        --verbose true  --num_threads_read $NUM_READERS --num_threads_write $NUM_WRITERS \
                        --num_reads $NUM_READS --num_writes $NUM_WRITES \
                        | tee ${FILE_PREFIX}${NONCE}.log
#tail -f ${FILE_PREFIX}${NONCE}.log
#                       --workload CassandraSecondaryIndex  \
#                       --workload SqlSecondaryIndex  \
#                       --workload CassandraSecondaryIndex  --cql_read_timeout_ms 6000 --socket_timeout 6000 \
#                       --nodes $HOSTS\

#                        --workload CassandraSecondaryIndex  \

#                        --nodes 127.0.0.2:5433 \
#                        --nodes 127.0.0.1:5433,127.0.0.2:5433,127.0.0.3:5433 \

#                        --workload SqlInserts  \
#                        --nodes 127.0.0.1:5433,127.0.0.2:5433,127.0.0.3:5433 \

#                       --workload CassandraKeyValue  \
#                       --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \

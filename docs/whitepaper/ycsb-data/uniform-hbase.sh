#/bin/bash

EXPORTER="com.yahoo.ycsb.measurements.exporter.JSONArrayMeasurementsExporter"
COMMON_FLAGS="-p recordcount=100000000 -p columnfamily=family -cp /etc/hbase/conf -p exporter=$EXPORTER -p table=ycsb_100m"
OUT_DIR=uniform-hbase

mkdir -p $OUT_DIR
if true ; then
	./bin/ycsb load hbase10 $COMMON_FLAGS  -p exportfile=$OUT_DIR/load.json -p clientbuffering=true \
	  -P workloads/workloada -p recordcount=100000000 -threads 16 -s 2>&1 | tee $OUT_DIR/load-100M.log
fi
for x in a b c d ; do
  dist_param=
  if [ "$x" != "d" ]; then
    dist_param="-p requestdistribution=uniform"
  fi
  ./bin/ycsb run hbase10 -P workloads/workload$x -p recordcount=100000000 -p operationcount=10000000 \
      $COMMON_FLAGS -p exportfile=$OUT_DIR/$x.json \
      $dist_param \
      -threads 64 -s 2>&1 | tee $OUT_DIR/run-workload$x.log
done


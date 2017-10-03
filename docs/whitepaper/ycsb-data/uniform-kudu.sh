#/bin/bash

EXPORTER="com.yahoo.ycsb.measurements.exporter.JSONArrayMeasurementsExporter"
COMMON_FLAGS="-p recordcount=100000000 -p exporter=$EXPORTER -p table_name=ycsb_100m -p masterQuorum=a1216"
OUT_DIR=uniform-kudu

mkdir -p $OUT_DIR
if true ; then
	./bin/ycsb load kudu $COMMON_FLAGS  -p exportfile=$OUT_DIR/load.json -p sync_ops=false \
	  -p pre_split_num_tablets=100 \
	  -P workloads/workloada -p recordcount=100000000 -threads 16 -s 2>&1 | tee $OUT_DIR/load-100M.log
fi
for x in a b c d ; do
  dist_param=
  if [ "$x" != "d" ]; then
    dist_param="-p requestdistribution=uniform"
  fi
  ./bin/ycsb run kudu -P workloads/workload$x -p recordcount=100000000 -p operationcount=10000000 -p sync_ops=true \
      $COMMON_FLAGS -p exportfile=$OUT_DIR/$x.json \
      $dist_param \
      -threads 64 -s 2>&1 | tee $OUT_DIR/run-workload$x.log
done


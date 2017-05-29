# Copyright (c) YugaByte, Inc.

#######################################
# yb_rocksutil
#######################################

set(ROCKSUTIL_SRCS
    yb_rocksdb.cc
    yb_rocksdb_logger.cc
    write_batch_formatter.cc)

set(ROCKSUTIL_LIBS
    gflags
    glog
    gutil
    rocksdb
    yb_util)

ADD_YB_LIBRARY(yb_rocksutil
                       SRCS ${ROCKSUTIL_SRCS}
                       DEPS ${ROCKSUTIL_LIBS})

add_dependencies(yb_rocksutil gen_version_info)

set(YB_TEST_LINK_LIBS yb_rocksutil ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(yb_rocksdb_logger-test)

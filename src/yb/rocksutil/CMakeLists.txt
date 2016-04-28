# Copyright (c) YugaByte, Inc.

#######################################
# yb_rocksutil
#######################################

set(ROCKSUTIL_SRCS
    yb_rocksdb_logger.cc)

set(ROCKSUTIL_LIBS
    gflags
    glog
    rocksdb)

ADD_EXPORTABLE_LIBRARY(yb_rocksutil
                       SRCS ${ROCKSUTIL_SRCS}
                       DEPS ${ROCKSUTIL_LIBS})

add_dependencies(yb_rocksutil gen_version_info)


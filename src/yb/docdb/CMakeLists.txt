# Copyright (c) YugaByte, Inc.

# docdb_proto

PROTOBUF_GENERATE_CPP(
  DOCDB_PROTO_SRCS DOCDB_PROTO_HDRS DOCDB_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES docdb.proto)

ADD_EXPORTABLE_LIBRARY(docdb_proto
  SRCS ${DOCDB_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${DOCDB_PROTO_TGTS})

set(DOCDB_SRCS
    doc_key.cc
    doc_kv_util.cc
    doc_operation.cc
    doc_rowwise_iterator.cc
    doc_write_batch_cache.cc
    docdb.cc
    docdb_compaction_filter.cc
    docdb_rocksdb_util.cc
    in_mem_docdb.cc
    internal_doc_iterator.cc
    key_bytes.cc
    primitive_value.cc
    value.cc
    subdocument.cc
    value_type.cc
    ysql_scanspec.cc)

set(DOCDB_DEPS
    rocksdb
    yb_util
    yb_rocksutil
    yb_common
    docdb_proto
    server_common)

ADD_EXPORTABLE_LIBRARY(yb_docdb
                       SRCS ${DOCDB_SRCS}
                       DEPS ${DOCDB_DEPS})

# A library with common code shared between DocDB tests.
set(DOCDB_TEST_COMMON_SRCS
    docdb_test_util.cc
    docdb_test_base.cc)

add_library(yb_docdb_test_common ${DOCDB_TEST_COMMON_SRCS})
target_link_libraries(yb_docdb_test_common
                      yb_docdb
                      yb_test_util)

set(YB_TEST_LINK_LIBS yb_docdb_test_common ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(doc_key-test)
ADD_YB_TEST(doc_kv_util-test)
ADD_YB_TEST(docdb-test)
ADD_YB_TEST(primitive_value-test)
ADD_YB_TEST(randomized_docdb-test)
ADD_YB_TEST(subdocument-test)
ADD_YB_TEST(doc_operation-test)

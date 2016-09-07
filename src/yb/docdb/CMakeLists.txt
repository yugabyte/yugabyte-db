# Copyright (c) YugaByte, Inc.

set(DOCDB_SRCS
    doc_key.cc
    doc_kv_util.cc
    doc_rowwise_iterator.cc
    doc_write_batch_cache.cc
    docdb.cc
    docdb_test_util.cc
    in_mem_docdb.cc
    internal_doc_iterator.cc
    primitive_value.cc
    subdocument.cc
    value_type.cc)

set(DOCDB_DEPS
    rocksdb
    yb_util
    yb_rocksutil
    yb_common)

ADD_EXPORTABLE_LIBRARY(yb_docdb
                       SRCS ${DOCDB_SRCS}
                       DEPS ${DOCDB_DEPS})

set(YB_TEST_LINK_LIBS yb_docdb ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(doc_key-test)
ADD_YB_TEST(doc_kv_util-test)
ADD_YB_TEST(docdb-test)
ADD_YB_TEST(primitive_value-test)
ADD_YB_TEST(subdocument-test)

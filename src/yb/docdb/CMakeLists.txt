# Copyright (c) YugaByte, Inc.

set(DOCDB_SRCS
    doc_key.cc
    doc_kv_util.cc
    docdb.cc
    internal_doc_iterator.cc
    primitive_value.cc
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

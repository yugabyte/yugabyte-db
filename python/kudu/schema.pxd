#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from libcpp.map cimport map

from libkudu_client cimport *


cdef class KuduType(object):
    cdef readonly:
        DataType type


cdef class ColumnSchema:
    """
    Wraps a Kudu client ColumnSchema object
    """
    cdef:
        KuduColumnSchema* schema
        KuduType _type


cdef class ColumnSpec:
    cdef:
        KuduColumnSpec* spec


cdef class SchemaBuilder:
    cdef:
        KuduSchemaBuilder builder


cdef class Schema:
    cdef:
        const KuduSchema* schema
        object parent
        bint own_schema
        map[string, int] _col_mapping
        bint _mapping_initialized

    cdef int get_loc(self, name) except -1

    cdef inline DataType loc_type(self, int i):
        return self.schema.Column(i).type()

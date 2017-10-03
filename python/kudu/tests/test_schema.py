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

from __future__ import division

from kudu.compat import unittest
import kudu


class TestSchema(unittest.TestCase):

    def setUp(self):
        self.columns = [('one', 'int32', False),
                        ('two', 'int8', False),
                        ('three', 'double', True),
                        ('four', 'string', False)]

        self.primary_keys = ['one', 'two']

        self.builder = kudu.schema_builder()
        for name, typename, nullable in self.columns:
            self.builder.add_column(name, typename, nullable=nullable)

        self.builder.set_primary_keys(self.primary_keys)
        self.schema = self.builder.build()

    def test_repr(self):
        result = repr(self.schema)
        for name, _, _ in self.columns:
            assert name in result

        assert 'PRIMARY KEY (one, two)' in result

    def test_schema_length(self):
        assert len(self.schema) == 4

    def test_names(self):
        assert self.schema.names == ['one', 'two', 'three', 'four']

    def test_primary_keys(self):
        assert self.schema.primary_key_indices() == [0, 1]
        assert self.schema.primary_keys() == ['one', 'two']

    def test_getitem_boundschecking(self):
        with self.assertRaises(IndexError):
            self.schema[4]

    def test_getitem_wraparound(self):
        # wraparound
        result = self.schema[-1]
        expected = self.schema[3]

        assert result.equals(expected)

    def test_getitem_string(self):
        result = self.schema['three']
        expected = self.schema[2]

        assert result.equals(expected)

        with self.assertRaises(KeyError):
            self.schema['not_found']

    def test_schema_equals(self):
        assert self.schema.equals(self.schema)

        builder = kudu.schema_builder()
        builder.add_column('key', 'int64', nullable=False, primary_key=True)
        schema = builder.build()

        assert not self.schema.equals(schema)

    def test_column_equals(self):
        assert not self.schema[0].equals(self.schema[1])

    def test_type(self):
        builder = kudu.schema_builder()
        (builder.add_column('key')
         .type('int32')
         .primary_key()
         .nullable(False))
        schema = builder.build()

        tp = schema[0].type
        assert tp.name == 'int32'
        assert tp.type == kudu.schema.INT32

    def test_compression(self):
        builder = kudu.schema_builder()
        builder.add_column('key', 'int64', nullable=False)

        foo = builder.add_column('foo', 'string').compression('lz4')
        assert foo is not None

        bar = builder.add_column('bar', 'string')
        bar.compression(kudu.COMPRESSION_ZLIB)

        with self.assertRaises(ValueError):
            bar = builder.add_column('qux', 'string', compression='unknown')

        builder.set_primary_keys(['key'])
        builder.build()

        # TODO; The C++ client does not give us an API to see the storage
        # attributes of a column

    def test_encoding(self):
        builder = kudu.schema_builder()
        builder.add_column('key', 'int64', nullable=False)

        foo = builder.add_column('foo', 'string').encoding('rle')
        assert foo is not None

        bar = builder.add_column('bar', 'string')
        bar.encoding(kudu.ENCODING_PLAIN)

        with self.assertRaises(ValueError):
            builder.add_column('qux', 'string', encoding='unknown')

        builder.set_primary_keys(['key'])
        builder.build()
        # TODO(wesm): The C++ client does not give us an API to see the storage
        # attributes of a column

    def test_set_column_spec_pk(self):
        builder = kudu.schema_builder()
        key = (builder.add_column('key', 'int64', nullable=False)
               .primary_key())
        assert key is not None
        schema = builder.build()
        assert 'key' in schema.primary_keys()

        builder = kudu.schema_builder()
        key = (builder.add_column('key', 'int64', nullable=False,
                                  primary_key=True))
        schema = builder.build()
        assert 'key' in schema.primary_keys()

    def test_partition_schema(self):
        pass

    def test_nullable_not_null(self):
        builder = kudu.schema_builder()
        (builder.add_column('key', 'int64', nullable=False)
         .primary_key())

        builder.add_column('data1', 'double').nullable(True)
        builder.add_column('data2', 'double').nullable(False)
        builder.add_column('data3', 'double', nullable=True)
        builder.add_column('data4', 'double', nullable=False)

        schema = builder.build()

        assert not schema[0].nullable
        assert schema[1].nullable
        assert not schema[2].nullable

        assert schema[3].nullable
        assert not schema[4].nullable

    def test_default_value(self):
        pass

    def test_column_schema_repr(self):
        result = repr(self.schema[0])
        expected = 'ColumnSchema(name=one, type=int32, nullable=False)'
        self.assertEqual(result, expected)

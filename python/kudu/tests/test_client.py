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

from kudu.compat import unittest, long
from kudu.tests.common import KuduTestBase
import kudu


class TestClient(KuduTestBase, unittest.TestCase):

    def setUp(self):
        pass

    def test_table_basics(self):
        table = self.client.table(self.ex_table)

        self.assertEqual(table.name, self.ex_table)
        self.assertEqual(table.num_columns, len(self.schema))

    def test_table_column(self):
        table = self.client.table(self.ex_table)
        col = table['key']

        assert col.name == b'key'
        assert col.parent is table

        result_repr = repr(col)
        expected_repr = ('Column(key, parent={0}, type=int32)'
                         .format(self.ex_table))
        assert result_repr == expected_repr

    def test_table_schema_retains_reference(self):
        import gc

        table = self.client.table(self.ex_table)
        schema = table.schema
        table = None

        gc.collect()
        repr(schema)

    def test_table_exists(self):
        self.assertFalse(self.client.table_exists('nonexistent-table'))
        self.assertTrue(self.client.table_exists(self.ex_table))

    def test_list_tables(self):
        schema = self.example_schema()

        to_create = ['foo1', 'foo2', 'foo3']
        for name in to_create:
            self.client.create_table(name, schema)

        result = self.client.list_tables()
        expected = [self.ex_table] + to_create
        assert sorted(result) == expected

        result = self.client.list_tables('foo')
        assert sorted(result) == to_create

        for name in to_create:
            self.client.delete_table(name)

    def test_is_multimaster(self):
        assert not self.client.is_multimaster

    def test_delete_table(self):
        name = "peekaboo"
        self.client.create_table(name, self.schema)
        self.client.delete_table(name)
        assert not self.client.table_exists(name)

        # Should raise a more meaningful exception at some point
        with self.assertRaises(kudu.KuduNotFound):
            self.client.delete_table(name)

    def test_table_nonexistent(self):
        self.assertRaises(kudu.KuduNotFound, self.client.table,
                          '__donotexist__')

    def test_insert_nonexistent_field(self):
        table = self.client.table(self.ex_table)
        op = table.new_insert()
        self.assertRaises(KeyError, op.__setitem__, 'doesntexist', 12)

    def test_insert_rows_and_delete(self):
        nrows = 100
        table = self.client.table(self.ex_table)
        session = self.client.new_session()
        for i in range(nrows):
            op = table.new_insert()
            op['key'] = i
            op['int_val'] = i * 2
            op['string_val'] = 'hello_%d' % i
            session.apply(op)

        # Cannot apply the same insert twice, C++ client does not indicate an
        # error
        self.assertRaises(Exception, session.apply, op)

        # synchronous
        session.flush()

        scanner = table.scanner().open()
        assert len(scanner.read_all_tuples()) == nrows

        # Delete the rows we just wrote
        for i in range(nrows):
            op = table.new_delete()
            op['key'] = i
            session.apply(op)
        session.flush()

        scanner = table.scanner().open()
        assert len(scanner.read_all_tuples()) == 0

    def test_session_auto_open(self):
        table = self.client.table(self.ex_table)
        scanner = table.scanner()
        result = scanner.read_all_tuples()
        assert len(result) == 0

    def test_session_open_idempotent(self):
        table = self.client.table(self.ex_table)
        scanner = table.scanner().open().open()
        result = scanner.read_all_tuples()
        assert len(result) == 0

    def test_session_flush_modes(self):
        self.client.new_session(flush_mode=kudu.FLUSH_MANUAL)
        self.client.new_session(flush_mode=kudu.FLUSH_AUTO_SYNC)

        self.client.new_session(flush_mode='manual')
        self.client.new_session(flush_mode='sync')

        with self.assertRaises(kudu.KuduNotSupported):
            self.client.new_session(flush_mode=kudu.FLUSH_AUTO_BACKGROUND)

        with self.assertRaises(kudu.KuduNotSupported):
            self.client.new_session(flush_mode='background')

        with self.assertRaises(ValueError):
            self.client.new_session(flush_mode='foo')

    def test_connect_timeouts(self):
        # it works! any other way to check
        kudu.connect(self.master_host, self.master_port,
                     admin_timeout_ms=100,
                     rpc_timeout_ms=100)

    def test_capture_kudu_error(self):
        pass


class TestMonoDelta(unittest.TestCase):

    def test_empty_ctor(self):
        delta = kudu.TimeDelta()
        assert repr(delta) == 'kudu.TimeDelta()'

    def test_static_ctors(self):
        delta = kudu.timedelta(3.5)
        assert delta.to_seconds() == 3.5

        delta = kudu.timedelta(millis=3500)
        assert delta.to_millis() == 3500

        delta = kudu.timedelta(micros=3500)
        assert delta.to_micros() == 3500

        delta = kudu.timedelta(micros=1000)
        assert delta.to_nanos() == long(1000000)

        delta = kudu.timedelta(nanos=3500)
        assert delta.to_nanos() == 3500

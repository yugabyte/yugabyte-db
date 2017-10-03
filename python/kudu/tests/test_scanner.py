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
from kudu.tests.common import KuduTestBase
import kudu


class TestScanner(KuduTestBase, unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        super(TestScanner, cls).setUpClass()

        cls.nrows = 100
        table = cls.client.table(cls.ex_table)
        session = cls.client.new_session()

        tuples = []
        for i in range(cls.nrows):
            op = table.new_insert()
            tup = i, i * 2, 'hello_%d' % i if i % 2 == 0 else None
            op['key'] = tup[0]
            op['int_val'] = tup[1]
            if i % 2 == 0:
                op['string_val'] = tup[2]
            session.apply(op)
            tuples.append(tup)
        session.flush()

        cls.table = table
        cls.tuples = tuples

    @classmethod
    def tearDownClass(cls):
        pass

    def setUp(self):
        pass

    def test_scan_rows_basic(self):
        # Let's scan with no predicates
        scanner = self.table.scanner().open()

        tuples = scanner.read_all_tuples()
        self.assertEqual(sorted(tuples), self.tuples)

    def test_scan_rows_simple_predicate(self):
        key = self.table['key']
        preds = [key >= 20, key <= 49]

        def _read_predicates(preds):
            scanner = self.table.scanner()
            scanner.add_predicates(preds)
            scanner.open()
            return scanner.read_all_tuples()

        tuples = _read_predicates(preds)
        self.assertEqual(sorted(tuples), self.tuples[20:50])

        # verify predicates reusable
        tuples = _read_predicates(preds)
        self.assertEqual(sorted(tuples), self.tuples[20:50])

    def test_scan_rows_string_predicate(self):
        scanner = self.table.scanner()

        sv = self.table['string_val']

        scanner.add_predicates([sv >= 'hello_20',
                                sv <= 'hello_22'])

        scanner.set_fault_tolerant()
        scanner.open()

        tuples = scanner.read_all_tuples()

        self.assertEqual(sorted(tuples), [(20, 40, 'hello_20'), (22, 44, 'hello_22')])

    def test_scan_invalid_predicates(self):
        scanner = self.table.scanner()
        sv = self.table['string_val']

        with self.assertRaises(TypeError):
            scanner.add_predicates([sv >= None])

        with self.assertRaises(kudu.KuduInvalidArgument):
            scanner.add_predicates([sv >= 1])

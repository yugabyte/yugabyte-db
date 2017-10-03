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

# distutils: language = c++
# cython: embedsignature = True

from libcpp.string cimport string
from libcpp cimport bool as c_bool

cimport cpython
from cython.operator cimport dereference as deref

from libkudu_client cimport *

from kudu.compat import tobytes, frombytes
from kudu.schema cimport Schema, ColumnSchema
from kudu.errors cimport check_status
from errors import KuduException

import six


cdef class TimeDelta:
    """
    Wrapper interface for kudu MonoDelta class, which is used to specify
    timedeltas for timeouts and other uses.
    """

    cdef:
        MonoDelta delta

    def __cinit__(self):
        pass

    @staticmethod
    def from_seconds(seconds):
        """
        Construct a new TimeDelta from fractional seconds.

        Parameters
        ----------
        seconds : double

        Returns
        -------
        delta : TimeDelta
        """
        cdef TimeDelta result = TimeDelta()
        result.init(MonoDelta.FromSeconds(seconds))
        return result

    @staticmethod
    def from_millis(int64_t ms):
        """
        Construct a new TimeDelta from integer milliseconds.

        Parameters
        ----------
        ms : int

        Returns
        -------
        delta : TimeDelta
        """
        cdef TimeDelta result = TimeDelta()
        result.init(MonoDelta.FromMilliseconds(ms))
        return result

    @staticmethod
    def from_micros(int64_t us):
        """
        Construct a new TimeDelta from integer microseconds.

        Parameters
        ----------
        us : int

        Returns
        -------
        delta : TimeDelta
        """
        cdef TimeDelta result = TimeDelta()
        result.init(MonoDelta.FromMicroseconds(us))
        return result

    @staticmethod
    def from_nanos(seconds):
        """
        Construct a new TimeDelta from integer nanoseconds.

        Parameters
        ----------
        ns : int

        Returns
        -------
        delta : TimeDelta
        """
        cdef TimeDelta result = TimeDelta()
        result.init(MonoDelta.FromNanoseconds(seconds))
        return result

    cpdef double to_seconds(self):
        """
        Return timedelta as fractional seconds.
        """
        return self.delta.ToSeconds()

    cpdef int64_t to_millis(self):
        """
        Return timedelta as exact milliseconds.
        """
        return self.delta.ToMilliseconds()

    cpdef int64_t to_micros(self):
        """
        Return timedelta as exact microseconds.
        """
        return self.delta.ToMicroseconds()

    cpdef int64_t to_nanos(self):
        """
        Return timedelta as exact nanoseconds.
        """
        return self.delta.ToNanoseconds()

    cdef init(self, const MonoDelta& val):
        self.delta = val

    def __repr__(self):
        cdef object as_string

        if self.delta.Initialized():
            as_string = self.delta.ToString()
            return 'kudu.TimeDelta({0})'.format(as_string)
        else:
            return 'kudu.TimeDelta()'

    def __richcmp__(TimeDelta self, TimeDelta other, int op):
        if op == cpython.Py_EQ:
            return self.delta.Equals(other.delta)
        elif op == cpython.Py_NE:
            return not self.delta.Equals(other.delta)
        elif op == cpython.Py_LT:
            return self.delta.LessThan(other.delta)
        elif op == cpython.Py_LE:
            return not self.delta.MoreThan(other.delta)
        elif op == cpython.Py_GT:
            return self.delta.MoreThan(other.delta)
        elif op == cpython.Py_GE:
            return not self.delta.LessThan(other.delta)
        else:
            raise ValueError('invalid operation: {0}'.format(op))


cdef class Client:

    """
    The primary class for interacting with a Kudu cluster. Can connect to one
    or more Kudu master servers. Do not instantiate this class directly; use
    kudu.connect instead.
    """

    cdef:
        shared_ptr[KuduClient] client
        KuduClient* cp

    cdef readonly:
        list master_addrs

    def __cinit__(self, addr_or_addrs, admin_timeout_ms=None,
                  rpc_timeout_ms=None):
        cdef:
            string c_addr
            vector[string] c_addrs
            KuduClientBuilder builder
            TimeDelta timeout

        if isinstance(addr_or_addrs, six.string_types):
            addr_or_addrs = [addr_or_addrs]
        elif not isinstance(addr_or_addrs, list):
            addr_or_addrs = list(addr_or_addrs)

        self.master_addrs = addr_or_addrs
        for addr in addr_or_addrs:
            c_addrs.push_back(tobytes(addr))

        builder.master_server_addrs(c_addrs)

        if admin_timeout_ms is not None:
            timeout = TimeDelta.from_millis(admin_timeout_ms)
            builder.default_admin_operation_timeout(timeout.delta)

        if rpc_timeout_ms is not None:
            timeout = TimeDelta.from_millis(rpc_timeout_ms)
            builder.default_rpc_timeout(timeout.delta)

        check_status(builder.Build(&self.client))

        # A convenience
        self.cp = self.client.get()

    def __dealloc__(self):
        self.close()

    property is_multimaster:

        def __get__(self):
            return self.cp.IsMultiMaster()

    cpdef close(self):
        # Nothing yet to clean up here
        pass

    def create_table(self, table_name, Schema schema):
        """
        Creates a new Kudu table from the passed Schema and options.

        Parameters
        ----------
        table_name : string
        schema : kudu.Schema
          Create using kudu.schema_builder
        """
        cdef:
            KuduTableCreator* c
            Status s
        c = self.cp.NewTableCreator()
        try:
            s = (c.table_name(tobytes(table_name))
                 .schema(schema.schema)
                 .Create())
            check_status(s)
        finally:
            del c

    def delete_table(self, table_name):
        """
        Delete a Kudu table. Raises KuduNotFound if the table does not exist.

        Parameters
        ----------
        table_name : string
        """
        check_status(self.cp.DeleteTable(tobytes(table_name)))

    def table_exists(self, table_name):
        """Return True if the indicated table exists in the Kudu cluster.

        Parameters
        ----------
        table_name : string

        Returns
        -------
        exists : bool

        """
        cdef:
            string c_name = tobytes(table_name)
            c_bool exists

        check_status(self.cp.TableExists(c_name, &exists))
        return exists

    def table(self, table_name):
        """
        Construct a kudu.Table and retrieve its schema from the cluster.

        Raises KuduNotFound if the table does not exist.

        Parameters
        ----------
        table_name : string

        Returns
        -------
        table : kudu.Table
        """
        table_name = tobytes(table_name)
        cdef Table table = Table(table_name, self)

        check_status(self.cp.OpenTable(table_name, &table.table))
        table.init()
        return table

    def list_tables(self, match_substring=None):
        """
        Retrieve a list of table names in the Kudu cluster with an optional
        substring filter.

        Parameters
        ----------
        match_substring : string, optional
          If passed, the string must be exactly contained in the table names

        Returns
        -------
        tables : list[string]
          Table names returned from Kudu
        """
        cdef:
            vector[string] tables
            string c_match
            size_t i

        if match_substring is not None:
            c_match = tobytes(match_substring)
            check_status(self.cp.ListTables(&tables, c_match))
        else:
            check_status(self.cp.ListTables(&tables))

        result = []
        for i in range(tables.size()):
            result.append(frombytes(tables[i]))
        return result

    def new_session(self, flush_mode='manual', timeout_ms=5000):
        """
        Create a new KuduSession for applying write operations.

        Parameters
        ----------
        flush_mode : {'manual', 'sync', 'background'}, default 'manual'
          See Session.set_flush_mode
        timeout_ms : int, default 5000
          Timeout in milliseconds

        Returns
        -------
        session : kudu.Session
        """
        cdef Session result = Session()
        result.s = self.cp.NewSession()

        result.set_flush_mode(flush_mode)
        result.set_timeout_ms(timeout_ms)

        return result



#----------------------------------------------------------------------
# Handle marshalling Python values to raw values. Since range predicates
# require a const void*, this is one valid (though a bit verbose)
# approach. Note that later versions of Cython handle many Python -> C type
# casting problems (and integer overflows), but these should all be tested
# rigorously in our test suite


cdef class RawValue:
    cdef:
        void* data

    def __cinit__(self):
        self.data = NULL


cdef class Int8Val(RawValue):
    cdef:
        int8_t val

    def __cinit__(self, obj):
        self.val = <int8_t> obj
        self.data = &self.val


cdef class Int16Val(RawValue):
    cdef:
        int16_t val

    def __cinit__(self, obj):
        self.val = <int16_t> obj
        self.data = &self.val


cdef class Int32Val(RawValue):
    cdef:
        int32_t val

    def __cinit__(self, obj):
        self.val = <int32_t> obj
        self.data = &self.val


cdef class Int64Val(RawValue):
    cdef:
        int64_t val

    def __cinit__(self, obj):
        self.val = <int64_t> obj
        self.data = &self.val


cdef class DoubleVal(RawValue):
    cdef:
        double val

    def __cinit__(self, obj):
        self.val = <double> obj
        self.data = &self.val


cdef class FloatVal(RawValue):
    cdef:
        float val

    def __cinit__(self, obj):
        self.val = <float> obj
        self.data = &self.val


cdef class BoolVal(RawValue):
    cdef:
        c_bool val

    def __cinit__(self, obj):
        self.val = <c_bool> obj
        self.data = &self.val


cdef class StringVal(RawValue):
    cdef:
        # Python "str" object that was passed into the constructor.
        # We hold a reference to this so that the underlying data
        # doesn't go out of scope.
        object py_str
        # Heap-allocated Slice object, owned by this instance,
        # which points to the data in 'py_str'
        cdef Slice* val

    def __cinit__(self, obj):
        self.py_str = obj
        self.val = new Slice(<char*>self.py_str, len(self.py_str))
        # The C++ API expects a Slice* to be passed to the range predicate
        # constructor.
        self.data = self.val

    def __dealloc__(self):
        del self.val

#----------------------------------------------------------------------


cdef class Table:

    """
    Represents a Kudu table, containing the schema and other tools. Create by
    using the kudu.Client.table method after connecting to a cluster.
    """

    cdef:
        shared_ptr[KuduTable] table

    cdef readonly:
        object _name
        Schema schema
        Client parent

    def __cinit__(self, name, Client client):
        self._name = name
        self.parent = client

        # Users should not instantiate directly
        self.schema = Schema()

    cdef init(self):
        # Called after the refptr has been populated
        self.schema.schema = &self.ptr().schema()
        self.schema.own_schema = 0
        self.schema.parent = self

    def __len__(self):
        # TODO: is this cheaply knowable?
        raise NotImplementedError

    def __getitem__(self, key):
        spec = self.schema[key]
        return Column(self, key, spec)

    property name:

        def __get__(self):
            return frombytes(self.ptr().name())

    # XXX: don't love this name
    property num_columns:

        def __get__(self):
            return len(self.schema)

    def rename(self, new_name):
        raise NotImplementedError

    def drop(self):
        raise NotImplementedError

    def new_insert(self):
        """
        Create a new Insert operation. Pass the completed Insert to a Session.

        Returns
        -------
        insert : Insert
        """
        return Insert(self)

    def new_update(self):
        """
        Create a new Update operation. Pass the completed Update to a Session.

        Returns
        -------
        update : Update
        """
        return Update(self)

    def new_delete(self):
        """
        Create a new Delete operation. Pass the completed Update to a Session.

        Returns
        -------
        delete : Delete
        """
        return Delete(self)

    def scanner(self):
        """
        Create a new scanner for this table for retrieving a selection of table
        rows.

        Examples
        --------
        scanner = table.scanner()
        scanner.add_predicate(table['key'] > 10)
        scanner.open()
        batch = scanner.read_all()
        tuples = batch.as_tuples()

        Returns
        -------
        scanner : kudu.Scanner
        """
        cdef Scanner result = Scanner(self)
        result.scanner = new KuduScanner(self.ptr())
        return result

    cdef inline KuduTable* ptr(self):
        return self.table.get()


cdef class Column:

    """
    A reference to a Kudu table column intended to simplify creating predicates
    and other column-specific operations.

    Write arithmetic comparisons to create new Predicate objects that can be
    passed to a Scanner.

    Examples
    --------
    scanner.add_predicate(table[col_name] <= 10)
    """
    cdef readonly:
        object name
        Table parent
        ColumnSchema spec

    def __cinit__(self, Table parent, object name, ColumnSchema spec):
        self.name = tobytes(name)
        self.parent = parent
        self.spec = spec

    def __repr__(self):
        result = ('Column({0}, parent={1}, type={2})'
                  .format(frombytes(self.name),
                          self.parent.name,
                          self.spec.type.name))
        return result

    cdef KuduValue* box_value(self, object obj) except NULL:
        cdef:
            KuduValue* val
            Slice* slc

        if isinstance(obj, unicode):
            obj = obj.encode('utf8')

        if isinstance(obj, bytes):
            slc = new Slice(<char*> obj, len(obj))
            val = KuduValue.CopyString(deref(slc))
            del slc
        elif isinstance(obj, int):
            val = KuduValue.FromInt(obj)
        elif isinstance(obj, float):
            val = KuduValue.FromDouble(obj)
        else:
            raise TypeError(obj)

        return val

    def __richcmp__(Column self, value, int op):
        cdef:
            KuduPredicate* pred
            KuduValue* val
            Slice* col_name_slice
            ComparisonOp cmp_op
            Predicate result

        col_name_slice = new Slice(<char*> self.name,
                                   len(self.name))

        try:
            if op == 1: # <=
                cmp_op = KUDU_LESS_EQUAL
            elif op == 2: # ==
                cmp_op = KUDU_EQUAL
            elif op == 5: # >=
                cmp_op = KUDU_GREATER_EQUAL
            else:
                raise NotImplementedError

            val = self.box_value(value)
            pred = (self.parent.ptr()
                    .NewComparisonPredicate(deref(col_name_slice),
                                            cmp_op, val))
        finally:
            del col_name_slice

        result = Predicate()
        result.init(pred)

        return result


cdef class Predicate:

    """
    Wrapper for a KuduPredicate. Pass to Scanner.add_predicates
    """

    cdef:
        KuduPredicate* pred

    def __cinit__(self):
        self.pred = NULL

    def __dealloc__(self):
        if self.pred != NULL:
            del self.pred

    cdef init(self, KuduPredicate* pred):
        self.pred = pred


FLUSH_AUTO_SYNC = FlushMode_AutoSync
FLUSH_AUTO_BACKGROUND = FlushMode_AutoBackground
FLUSH_MANUAL = FlushMode_Manual

cdef dict _flush_modes = {
    'manual': FlushMode_Manual,
    'sync': FlushMode_AutoSync,
    'background': FlushMode_AutoBackground
}


cdef class Session:
    """
    Wrapper for a client KuduSession to build up write operations to interact
    with the cluster.
    """
    cdef:
        shared_ptr[KuduSession] s

    def __cinit__(self):
        pass

    def set_flush_mode(self, flush_mode='manual'):
        """
        Set the session operation flush mode

        Parameters
        ----------
        flush_mode : {'manual', 'sync', 'background'}, default 'manual'
          You can also use the constants FLUSH_MANUAL, FLUSH_AUTO_SYNC,
          and FLUSH_AUTO_BACKGROUND
        """
        cdef Status status
        cdef FlushMode fmode

        if isinstance(flush_mode, int):
            # todo: validation
            fmode = <FlushMode> flush_mode
        else:
            try:
                fmode = _flush_modes[flush_mode.lower()]
            except KeyError:
                raise ValueError('Invalid flush mode: {0}'
                                 .format(flush_mode))

        status = self.s.get().SetFlushMode(fmode)

        check_status(status)

    def set_timeout_ms(self, int64_t ms):
        """
        Set the session timeout in milliseconds
        """
        self.s.get().SetTimeoutMillis(ms)

    def apply(self, WriteOperation op):
        """
        Apply the indicated write operation

        Examples
        --------
        # Executes a single Insert operation
        session = client.new_session()
        op = table.new_insert()
        op['key'] = 0
        op['value1'] = 5
        op['value2'] = 3.5
        session.apply(op)
        session.flush()
        """
        return op.add_to_session(self)

    def flush(self):
        """
        Flush pending operations
        """
        check_status(self.s.get().Flush())

    def get_pending_errors(self):
        """
        Returns a list of buffered Kudu errors. A second value is returned
        indicating if there were more errors than could be stored in the
        session's error buffer (i.e. False means there was no error overflow)

        Returns
        -------
        errors, overflowed : list, bool
        """
        cdef:
            KuduError error
            vector[C_KuduError*] v_errors
            c_bool overflowed
            size_t i

        self.s.get().GetPendingErrors(&v_errors, &overflowed)

        result = []
        for i in range(v_errors.size()):
            error = KuduError()
            error.error = v_errors[i]
            result.append(error)

        return result, overflowed


cdef class Row:

    """
    A single row from a row batch
    """

    cdef:
        # So we can access the schema information
        Table table

        RowBatch parent

        # This object is owned by the parent RowBatch
        KuduRowResult* row

    def __cinit__(self, batch, table):
        self.parent = batch
        self.table = table
        self.row = NULL

    def __dealloc__(self):
        pass

    cdef tuple as_tuple(self):
        """
        Return the row as a Python tuple
        """
        cdef:
            int i, k
            tuple tup

        k = self.table.num_columns
        tup = cpython.PyTuple_New(k)
        for i in range(k):
            val = None

            if not self.is_null(i):
                val = self.get_slot(i)

            cpython.Py_INCREF(val)
            cpython.PyTuple_SET_ITEM(tup, i, val)

        return tup

    cdef inline get_bool(self, int i):
        cdef c_bool val
        check_status(self.row.GetBool(i, &val))
        # The built-in bool is masked by the libcpp typedef
        return bool(val)

    cdef inline get_int8(self, int i):
        cdef int8_t val
        check_status(self.row.GetInt8(i, &val))
        return val

    cdef inline get_int16(self, int i):
        cdef int16_t val
        check_status(self.row.GetInt16(i, &val))
        return val

    cdef inline get_int32(self, int i):
        cdef int32_t val
        check_status(self.row.GetInt32(i, &val))
        return val

    cdef inline get_int64(self, int i):
        cdef int64_t val
        check_status(self.row.GetInt64(i, &val))
        return val

    cdef inline get_double(self, int i):
        cdef double val
        check_status(self.row.GetDouble(i, &val))
        return val

    cdef inline get_float(self, int i):
        cdef float val
        check_status(self.row.GetFloat(i, &val))
        return val

    cdef inline get_string(self, int i):
        cdef Slice val
        check_status(self.row.GetString(i, &val))
        return cpython.PyBytes_FromStringAndSize(<char*> val.mutable_data(),
                                                 val.size())

    cdef inline get_slot(self, int i):
        cdef:
            Status s
            DataType t = self.table.schema.loc_type(i)

        if t == KUDU_BOOL:
            return self.get_bool(i)
        elif t == KUDU_INT8:
            return self.get_int8(i)
        elif t == KUDU_INT16:
            return self.get_int16(i)
        elif t == KUDU_INT32:
            return self.get_int32(i)
        elif t == KUDU_INT64:
            return self.get_int64(i)
        elif t == KUDU_DOUBLE:
            return self.get_double(i)
        elif t == KUDU_FLOAT:
            return self.get_float(i)
        elif t == KUDU_STRING:
            return frombytes(self.get_string(i))
        else:
            raise TypeError(t)

    cdef inline bint is_null(self, int i):
        return self.row.IsNull(i)


cdef class RowBatch:
    """
    Class holding a batch of rows from a Scanner
    """
    # This class owns the KuduRowResult data
    cdef:
        Table table
        vector[KuduRowResult] rows

    def __cinit__(self, Table table):
        self.table = table

    def __len__(self):
        return self.rows.size()

    def __getitem__(self, i):
        return self.get_row(i).as_tuple()

    def __iter__(self):
        cdef int i = 0
        for i in range(len(self)):
            yield self.get_row(i).as_tuple()

    def as_tuples(self):
        """
        Return RowBatch as a list of Python tuples

        To simplify testing for the moment.
        """
        cdef list tuples = []
        for i in range(self.rows.size()):
            tuples.append(self.get_row(i).as_tuple())
        return tuples

    cdef Row get_row(self, i):
        # TODO: boundscheck

        # For safety, we need to increment the parent reference count and hold
        # on to a reference internally so that if the RowBatch goes out of
        # scope we won't end up with orphaned Row objects. This isn't the best,
        # but an intermediate solution until we can do something better..
        #
        # One alternative is to copy the KuduRowResult into the Row, but that
        # doesn't feel right.
        cdef Row row = Row(self, self.table)
        row.row = &self.rows[i]

        return row


cdef class Scanner:
    """
    A class for defining a selection of data we wish to scan out of a Kudu
    table. Create a scanner using Table.scanner.
    """

    cdef:
        Table table
        KuduScanner* scanner
        bint is_open

    def __cinit__(self, Table table):
        self.table = table
        self.scanner = NULL
        self.is_open = 0

    def __dealloc__(self):
        # We own this one
        if self.scanner != NULL:
            del self.scanner

    cdef inline ensure_open(self):
        if not self.is_open:
            self.open()

    def add_predicates(self, preds):
        """
        Add a list of scan predicates to the scanner. Select columns from the
        parent table and make comparisons to create predicates.

        Examples
        --------
        c = table[col_name]
        preds = [c >= 0, c <= 10]
        scanner.add_predicates(preds)

        Parameters
        ----------
        preds : list of Predicate
        """
        for pred in preds:
            self.add_predicate(pred)

    cpdef add_predicate(self, Predicate pred):
        """
        Add a scan predicates to the scanner. Select columns from the
        parent table and make comparisons to create predicates.

        Examples
        --------
        pred = table[col_name] <= 10
        scanner.add_predicate(pred)

        Parameters
        ----------
        pred : kudu.Predicate
        """
        cdef KuduPredicate* clone

        # We clone the KuduPredicate so that the Predicate wrapper class can be
        # reused
        clone = pred.pred.Clone()
        check_status(self.scanner.AddConjunctPredicate(clone))

    def set_fault_tolerant(self):
        """
        Makes the underlying KuduScanner fault tolerant.
        Returns a reference to itself to facilitate chaining.

        Returns
        -------
        self : Scanner
        """
        check_status(self.scanner.SetFaultTolerant())
        return self

    def open(self):
        """
        Returns a reference to itself to facilitate chaining

        Returns
        -------
        self : Scanner
        """
        if not self.is_open:
            check_status(self.scanner.Open())
            self.is_open = 1
        return self

    def has_more_rows(self):
        """
        Returns True if there are more rows to be read.
        """
        return self.scanner.HasMoreRows()

    def read_all_tuples(self):
        """
        Compute a RowBatch containing all rows from the scan operation (which
        hopefully fit into memory, probably not handled gracefully at the
        moment).
        """
        cdef list tuples = []
        cdef RowBatch batch

        self.ensure_open()

        while self.has_more_rows():
            batch = self.next_batch()
            tuples.extend(batch.as_tuples())

        return tuples

    def read_next_batch_tuples(self):
        return self.next_batch().as_tuples()

    cdef RowBatch next_batch(self):
        """
        Retrieve the next batch of rows from the scanner.

        Returns
        -------
        batch : RowBatch
        """
        if not self.has_more_rows():
            raise StopIteration

        cdef RowBatch batch = RowBatch(self.table)
        check_status(self.scanner.NextBatch(&batch.rows))
        return batch


cdef class KuduError:

    """
    Wrapper for a C++ KuduError indicating a client error resulting from
    applying operations in a session.
    """

    cdef:
        C_KuduError* error

    def __cinit__(self):
        self.error = NULL

    def __dealloc__(self):
        # We own this object
        if self.error != NULL:
            del self.error

    def failed_op(self):
        raise NotImplementedError

    def __repr__(self):
        return "KuduError('%s')" % (self.error.status().ToString())


cdef class WriteOperation:
    cdef:
        Table table
        KuduPartialRow* row
        bint applied

    def __cinit__(self, Table table):
        # This gets called before any subclass cinit methods
        self.table = table
        self.applied = 0

    def __setitem__(self, key, value):
        if isinstance(key, basestring):
            self.set_field(key, value)
        else:
            self.set_loc(key, value)

    cpdef set_field(self, key, value):
        cdef:
            int i = self.table.schema.get_loc(key)
            DataType t = self.table.schema.loc_type(i)
            cdef Slice* slc

        # Leave it to Cython to do the coercion and complain if it doesn't
        # work. Cython will catch many casting problems but we should verify
        # with unit tests.
        if t == KUDU_BOOL:
            self.row.SetBool(i, <c_bool> value)
        elif t == KUDU_INT8:
            self.row.SetInt8(i, <int8_t> value)
        elif t == KUDU_INT16:
            self.row.SetInt16(i, <int16_t> value)
        elif t == KUDU_INT32:
            self.row.SetInt32(i, <int32_t> value)
        elif t == KUDU_INT64:
            self.row.SetInt64(i, <int64_t> value)
        elif t == KUDU_FLOAT:
            self.row.SetFloat(i, <float> value)
        elif t == KUDU_DOUBLE:
            self.row.SetDouble(i, <double> value)
        elif t == KUDU_STRING:
            if not cpython.PyBytes_Check(value):
                value = value.encode('utf8')

            # TODO: It would be much better not to heap-allocate a Slice object
            slc = new Slice(cpython.PyBytes_AsString(value))

            # Not safe to take a reference to PyBytes data for now
            self.row.SetStringCopy(i, deref(slc))
            del slc

    cpdef set_loc(self, int i, value):
        pass

    cpdef set_field_null(self, key):
        pass

    cpdef set_loc_null(self, int i):
        pass

    cdef add_to_session(self, Session s):
        pass


cdef class Insert(WriteOperation):
    cdef:
        KuduInsert* op

    def __cinit__(self, Table table):
        self.op = self.table.ptr().NewInsert()
        self.row = self.op.mutable_row()

    def __dealloc__(self):
        del self.op

    cdef add_to_session(self, Session s):
        if self.applied:
            raise Exception

        check_status(s.s.get().Apply(self.op))
        self.op = NULL
        self.applied = 1


cdef class Update(WriteOperation):
    cdef:
        KuduUpdate* op

    def __cinit__(self, Table table):
        self.table = table
        self.op = table.ptr().NewUpdate()
        self.row = self.op.mutable_row()

    def __dealloc__(self):
        del self.op

    cdef add_to_session(self, Session s):
        pass


cdef class Delete(WriteOperation):
    cdef:
        KuduDelete* op

    def __cinit__(self, Table table):
        self.table = table
        self.op = table.ptr().NewDelete()
        self.row = self.op.mutable_row()

    def __dealloc__(self):
        del self.op

    cdef add_to_session(self, Session s):
        if self.applied:
            raise Exception

        check_status(s.s.get().Apply(self.op))
        self.applied = 1
        self.op = NULL



cdef inline cast_pyvalue(DataType t, object o):
    if t == KUDU_BOOL:
        return BoolVal(o)
    elif t == KUDU_INT8:
        return Int8Val(o)
    elif t == KUDU_INT16:
        return Int16Val(o)
    elif t == KUDU_INT32:
        return Int32Val(o)
    elif t == KUDU_INT64:
        return Int64Val(o)
    elif t == KUDU_DOUBLE:
        return DoubleVal(o)
    elif t == KUDU_FLOAT:
        return FloatVal(o)
    elif t == KUDU_STRING:
        return StringVal(o)
    else:
        raise TypeError(t)

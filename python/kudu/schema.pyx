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

from cython.operator cimport dereference as deref

from kudu.compat import tobytes, frombytes
from kudu.schema cimport *
from kudu.errors cimport check_status

import six

from . import util

BOOL = KUDU_BOOL
STRING = KUDU_STRING

INT8 = KUDU_INT8
INT16 = KUDU_INT16
INT32 = KUDU_INT32
INT64 = KUDU_INT64

FLOAT = KUDU_FLOAT
DOUBLE = KUDU_DOUBLE

TIMESTAMP = KUDU_TIMESTAMP
BINARY = KUDU_BINARY


cdef dict _reverse_dict(d):
    return dict((v, k) for k, v in d.items())


# CompressionType enums
COMPRESSION_DEFAULT = CompressionType_DEFAULT
COMPRESSION_NONE = CompressionType_NONE
COMPRESSION_SNAPPY = CompressionType_SNAPPY
COMPRESSION_LZ4 = CompressionType_LZ4
COMPRESSION_ZLIB = CompressionType_ZLIB

cdef dict _compression_types = {
    'default': COMPRESSION_DEFAULT,
    'none': COMPRESSION_NONE,
    'snappy': COMPRESSION_SNAPPY,
    'lz4': COMPRESSION_LZ4,
    'zlib': COMPRESSION_ZLIB,
}

cdef dict _compression_type_to_name = _reverse_dict(_compression_types)


# EncodingType enums
ENCODING_AUTO = EncodingType_AUTO
ENCODING_PLAIN = EncodingType_PLAIN
ENCODING_PREFIX = EncodingType_PREFIX
ENCODING_GROUP_VARINT = EncodingType_GROUP_VARINT
ENCODING_RLE = EncodingType_RLE

cdef dict _encoding_types = {
    'auto': ENCODING_AUTO,
    'plain': ENCODING_PLAIN,
    'prefix': ENCODING_PREFIX,
    'group_varint': ENCODING_GROUP_VARINT,
    'rle': ENCODING_RLE,
}

cdef dict _encoding_type_to_name = _reverse_dict(_encoding_types)


cdef class KuduType(object):

    """
    Usability wrapper for Kudu data type enum
    """

    def __cinit__(self, DataType type):
        self.type = type

    property name:

        def __get__(self):
            return _type_names[self.type]

    def __repr__(self):
        return 'KuduType({0})'.format(self.name)


int8 = KuduType(KUDU_INT8)
int16 = KuduType(KUDU_INT16)
int32 = KuduType(KUDU_INT32)
int64 = KuduType(KUDU_INT64)
string_ = KuduType(KUDU_STRING)
bool_ = KuduType(KUDU_BOOL)
float_ = KuduType(KUDU_FLOAT)
double_ = KuduType(KUDU_DOUBLE)
binary = KuduType(KUDU_BINARY)
timestamp = KuduType(KUDU_TIMESTAMP)


cdef dict _type_names = {
    INT8: 'int8',
    INT16: 'int16',
    INT32: 'int32',
    INT64: 'int64',
    STRING: 'string',
    BOOL: 'bool',
    FLOAT: 'float',
    DOUBLE: 'double',
    BINARY: 'binary',
    TIMESTAMP: 'timestamp'
}


cdef dict _type_name_to_number = _reverse_dict(_type_names)

cdef dict _type_to_obj = {
    INT8: int8,
    INT16: int16,
    INT32: int32,
    INT64: int64,
    STRING: string_,
    BOOL: bool_,
    FLOAT: float_,
    DOUBLE: double_,
    BINARY: binary,
    TIMESTAMP: timestamp
}


cdef KuduType to_data_type(object obj):
    if isinstance(obj, KuduType):
        return obj
    elif isinstance(obj, six.string_types):
        return _type_to_obj[_type_name_to_number[obj]]
    elif obj in _type_to_obj:
        return _type_to_obj[obj]
    else:
        raise ValueError('Invalid type: {0}'.format(obj))


cdef class ColumnSchema:
    """
    Wraps a Kudu client ColumnSchema object. Use schema.at(i) or schema[i] to
    construct one.
    """

    def __cinit__(self):
        self.schema = NULL
        self._type = None

    def __dealloc__(self):
        if self.schema is not NULL:
            del self.schema

    property name:
        def __get__(self):
            return frombytes(self.schema.name())

    property type:
        def __get__(self):
            if self._type is None:
                self._type = _type_to_obj[self.schema.type()]
            return self._type

    property nullable:
        def __get__(self):
            return self.schema.is_nullable()

    def equals(self, other):
        if not isinstance(other, ColumnSchema):
            return False
        return self.schema.Equals(deref((<ColumnSchema> other).schema))

    def __repr__(self):
        return ('ColumnSchema(name=%s, type=%s, nullable=%s)'
                % (self.name, self.type.name,
                   self.nullable))


#----------------------------------------------------------------------

cdef class ColumnSpec:

    """
    Helper class for configuring a column's settings while using the
    SchemaBuilder.
    """

    def type(self, type_):
        self.spec.Type(to_data_type(type_).type)
        return self

    def default(self, value):
        """
        Set a default value for the column
        """
        raise NotImplementedError

    def clear_default(self):
        """
        Remove a default value set.
        """
        raise NotImplementedError

    def compression(self, compression):
        """
        Set the compression type

        Parameters
        ----------
        compression : string or int
          One of {'default', 'none', 'snappy', 'lz4', 'zlib'}
          Or see kudu.COMPRESSION_* constants

        Returns
        -------
        self
        """
        cdef CompressionType type
        if isinstance(compression, int):
            # todo: validation
            type = <CompressionType> compression
        else:
            if compression is None:
                type = CompressionType_NONE
            else:
                try:
                    type = _compression_types[compression.lower()]
                except KeyError:
                    raise ValueError('Invalid compression type: {0}'
                                     .format(compression))

        self.spec.Compression(type)
        return self

    def encoding(self, encoding):
        """
        Set the encoding type

        Parameters
        ----------
        encoding : string or int
          One of {'auto', 'plain', 'prefix', 'group_varint', 'rle'}
          Or see kudu.ENCODING_* constants

        Returns
        -------
        self
        """
        cdef EncodingType type
        if isinstance(encoding, six.string_types):
            try:
                type = _encoding_types[encoding.lower()]
            except KeyError:
                raise ValueError('Invalid encoding type: {0}'
                                 .format(encoding))
        else:
            # todo: validation
            type = <EncodingType> encoding

        self.spec.Encoding(type)
        return self

    def primary_key(self):
        """
        Make this column a primary key. If you use this method, it will be the
        only primary key. Otherwise see set_primary_keys method on
        SchemaBuilder.

        Returns
        -------
        self
        """
        self.spec.PrimaryKey()
        return self

    def nullable(self, bint is_nullable=True):
        """
        Set nullable (True) or not nullable (False)

        Parameters
        ----------
        is_nullable : boolean, default True

        Returns
        -------
        self
        """
        if is_nullable:
            self.spec.Nullable()
        else:
            self.spec.NotNull()
        return self

    def rename(self, new_name):
        """
        Change the column name.

        TODO: Not implemented for table creation
        """
        self.spec.RenameTo(new_name)
        return self


cdef class SchemaBuilder:

    def add_column(self, name, type_=None, nullable=None, compression=None,
                   encoding=None, primary_key=False):
        """
        Add a new column to the schema. Returns a ColumnSpec object for further
        configuration and use in a fluid programming style.

        Parameters
        ----------
        name : string
        type_ : string or KuduType
          Data type e.g. 'int32' or kudu.int32
        nullable : boolean, default None
          New columns are nullable by default. Set boolean value for explicit
          nullable / not-nullable
        compression : string or int
          One of {'default', 'none', 'snappy', 'lz4', 'zlib'}
          Or see kudu.COMPRESSION_* constants
        encoding : string or int
          One of {'auto', 'plain', 'prefix', 'group_varint', 'rle'}
          Or see kudu.ENCODING_* constants
        primary_key : boolean, default False
          Use this column as the table primary key

        Examples
        --------
        (builder.add_column('foo')
         .nullable(True)
         .compression('lz4'))

        Returns
        -------
        spec : ColumnSpec
        """
        cdef:
            ColumnSpec result = ColumnSpec()
            string c_name = tobytes(name)

        result.spec = self.builder.AddColumn(c_name)

        if type_ is not None:
            result.type(type_)

        if nullable is not None:
            result.nullable(nullable)

        if compression is not None:
            result.compression(compression)

        if encoding is not None:
            result.encoding(encoding)

        if primary_key:
            result.primary_key()

        return result

    def set_primary_keys(self, key_names):
        """
        Set indicated columns (by name) to be the primary keys of the table
        schema

        Parameters
        ----------
        key_names : list of Python strings

        Returns
        -------
        None
        """
        cdef:
            vector[string] key_col_names

        for name in key_names:
            key_col_names.push_back(tobytes(name))

        self.builder.SetPrimaryKey(key_col_names)

    def build(self):
        """
        Creates an immutable Schema object after the user has finished adding
        and onfiguring columns

        Returns
        -------
        schema : Schema
        """
        cdef Schema result = Schema()
        cdef KuduSchema* schema = new KuduSchema()
        check_status(self.builder.Build(schema))

        result.schema = schema
        return result


cdef class Schema:

    """
    Container for a Kudu table schema. Obtain from Table instances or create
    new ones using kudu.SchemaBuilder
    """

    def __cinit__(self):
        # Users should not call this directly
        self.schema = NULL
        self.own_schema = 1
        self._col_mapping.clear()
        self._mapping_initialized = 0

    def __dealloc__(self):
        if self.schema is not NULL and self.own_schema:
            del self.schema

    property names:

        def __get__(self):
            result = []
            for i in range(self.schema.num_columns()):
                name = frombytes(self.schema.Column(i).name())
                result.append(name)

            return result

    def __repr__(self):
        # Got to be careful with huge schemas, maybe some kind of summary repr
        # when more than 20-30 columns?
        buf = six.StringIO()

        col_names = self.names
        space = 2 + max(len(x) for x in col_names)

        for i in range(len(self)):
            col = self.at(i)
            not_null = '' if col.nullable else ' NOT NULL'

            buf.write('\n{0}{1}{2}'
                      .format(col.name.ljust(space),
                              col.type.name, not_null))

        pk_string = ', '.join(col_names[i] for i in self.primary_key_indices())
        buf.write('\nPRIMARY KEY ({0})'.format(pk_string))

        return "kudu.Schema {{{0}\n}}".format(util.indent(buf.getvalue(), 2))

    def __len__(self):
        return self.schema.num_columns()

    def __getitem__(self, key):
        if isinstance(key, six.string_types):
            key = self.get_loc(key)

        if key < 0:
            key += len(self)
        return self.at(key)

    def equals(self, Schema other):
        """
        Returns True if the table schemas are equal
        """
        return self.schema.Equals(deref(other.schema))

    cdef int get_loc(self, name) except -1:
        if not self._mapping_initialized:
            for i in range(self.schema.num_columns()):
                self._col_mapping[self.schema.Column(i).name()] = i
            self._mapping_initialized = 1

        name = tobytes(name)

        # TODO: std::map is slightly verbose and inefficient here (O(lg n)
        # lookups), may consider replacing with a better / different hash table
        # should it become a performance bottleneck
        cdef map[string, int].iterator it = self._col_mapping.find(name)
        if it == self._col_mapping.end():
            raise KeyError(name)
        return self._col_mapping[name]

    def at(self, size_t i):
        """
        Return the ColumnSchema for a column index. Analogous to schema[i].

        Returns
        -------
        col_schema : ColumnSchema
        """
        cdef ColumnSchema result = ColumnSchema()

        if i < 0 or i >= self.schema.num_columns():
            raise IndexError('Column index {0} is not in range'
                             .format(i))

        result.schema = new KuduColumnSchema(self.schema.Column(i))

        return result

    def primary_key_indices(self):
        """
        Return the indices of the columns used as primary keys

        Returns
        -------
        key_indices : list[int]
        """
        cdef:
            vector[int] indices
            size_t i

        self.schema.GetPrimaryKeyColumnIndexes(&indices)

        result = []
        for i in range(indices.size()):
            result.append(indices[i])
        return result

    def primary_keys(self):
        """
        Return the names of the columns used as primary keys

        Returns
        -------
        key_names : list[str]
        """
        indices = self.primary_key_indices()
        return [self.at(i).name for i in indices]

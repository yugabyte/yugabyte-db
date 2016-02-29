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

from libc.stdint cimport *
from libcpp cimport bool as c_bool
from libcpp.string cimport string
from libcpp.vector cimport vector

# This must be included for cerr and other things to work
cdef extern from "<iostream>":
    pass

#----------------------------------------------------------------------
# Smart pointers and such

cdef extern from "yb/client/shared_ptr.h" namespace "yb::client::sp" nogil:

    cdef cppclass shared_ptr[T]:
        T* get()
        void reset()
        void reset(T* p)

cdef extern from "yb/util/status.h" namespace "yb" nogil:

    # We can later add more of the common status factory methods as needed
    cdef Status Status_OK "Status::OK"()

    cdef cppclass Status:
        Status()

        string ToString()

        Slice message()

        c_bool ok()
        c_bool IsNotFound()
        c_bool IsCorruption()
        c_bool IsNotSupported()
        c_bool IsIOError()
        c_bool IsInvalidArgument()
        c_bool IsAlreadyPresent()
        c_bool IsRuntimeError()
        c_bool IsNetworkError()
        c_bool IsIllegalState()
        c_bool IsNotAuthorized()
        c_bool IsAborted()


cdef extern from "yb/util/monotime.h" namespace "yb" nogil:

    # These classes are not yet needed directly but will need to be completed
    # from the C++ API
    cdef cppclass MonoDelta:
        MonoDelta()

        @staticmethod
        MonoDelta FromSeconds(double seconds)

        @staticmethod
        MonoDelta FromMilliseconds(int64_t ms)

        @staticmethod
        MonoDelta FromMicroseconds(int64_t us)

        @staticmethod
        MonoDelta FromNanoseconds(int64_t ns)

        c_bool Initialized()
        c_bool LessThan(const MonoDelta& other)
        c_bool MoreThan(const MonoDelta& other)
        c_bool Equals(const MonoDelta& other)

        string ToString()

        double ToSeconds()
        int64_t ToMilliseconds()
        int64_t ToMicroseconds()
        int64_t ToNanoseconds()

        # TODO, when needed
        # void ToTimeVal(struct timeval *tv)
        # void ToTimeSpec(struct timespec *ts)

        # @staticmethod
        # void NanosToTimeSpec(int64_t nanos, struct timespec* ts);


    cdef cppclass MonoTime:
        pass


cdef extern from "yb/client/schema.h" namespace "yb::client" nogil:

    enum DataType" yb::client::YBColumnSchema::DataType":
        YB_INT8 " yb::client::YBColumnSchema::INT8"
        YB_INT16 " yb::client::YBColumnSchema::INT16"
        YB_INT32 " yb::client::YBColumnSchema::INT32"
        YB_INT64 " yb::client::YBColumnSchema::INT64"
        YB_STRING " yb::client::YBColumnSchema::STRING"
        YB_BOOL " yb::client::YBColumnSchema::BOOL"
        YB_FLOAT " yb::client::YBColumnSchema::FLOAT"
        YB_DOUBLE " yb::client::YBColumnSchema::DOUBLE"
        YB_BINARY " yb::client::YBColumnSchema::BINARY"
        YB_TIMESTAMP " yb::client::YBColumnSchema::TIMESTAMP"

    enum EncodingType" yb::client::YBColumnStorageAttributes::EncodingType":
        EncodingType_AUTO " yb::client::YBColumnStorageAttributes::AUTO_ENCODING"
        EncodingType_PLAIN " yb::client::YBColumnStorageAttributes::PLAIN_ENCODING"
        EncodingType_PREFIX " yb::client::YBColumnStorageAttributes::PREFIX_ENCODING"
        EncodingType_GROUP_VARINT " yb::client::YBColumnStorageAttributes::GROUP_VARINT"
        EncodingType_RLE " yb::client::YBColumnStorageAttributes::RLE"

    enum CompressionType" yb::client::YBColumnStorageAttributes::CompressionType":
        CompressionType_DEFAULT " yb::client::YBColumnStorageAttributes::DEFAULT_COMPRESSION"
        CompressionType_NONE " yb::client::YBColumnStorageAttributes::NO_COMPRESSION"
        CompressionType_SNAPPY " yb::client::YBColumnStorageAttributes::SNAPPY"
        CompressionType_LZ4 " yb::client::YBColumnStorageAttributes::LZ4"
        CompressionType_ZLIB " yb::client::YBColumnStorageAttributes::ZLIB"

    cdef struct YBColumnStorageAttributes:
        YBColumnStorageAttributes()

        EncodingType encoding
        CompressionType compression
        string ToString()

    cdef cppclass YBColumnSchema:
        YBColumnSchema(const YBColumnSchema& other)
        YBColumnSchema(const string& name, DataType type)
        YBColumnSchema(const string& name, DataType type, c_bool is_nullable)
        YBColumnSchema(const string& name, DataType type, c_bool is_nullable,
                         const void* default_value)

        string& name()
        c_bool is_nullable()
        DataType type()

        c_bool Equals(YBColumnSchema& other)
        void CopyFrom(YBColumnSchema& other)

    cdef cppclass YBSchema:
        YBSchema()
        YBSchema(vector[YBColumnSchema]& columns, int key_columns)

        c_bool Equals(const YBSchema& other)
        YBColumnSchema Column(size_t idx)
        size_t num_columns()

        void GetPrimaryKeyColumnIndexes(vector[int]* indexes)

        YBPartialRow* NewRow()

    cdef cppclass YBColumnSpec:

         YBColumnSpec* Default(YBValue* value)
         YBColumnSpec* RemoveDefault()

         YBColumnSpec* Compression(CompressionType compression)
         YBColumnSpec* Encoding(EncodingType encoding)
         YBColumnSpec* BlockSize(int32_t block_size)

         YBColumnSpec* PrimaryKey()
         YBColumnSpec* NotNull()
         YBColumnSpec* Nullable()
         YBColumnSpec* Type(DataType type_)

         YBColumnSpec* RenameTo(string& new_name)


    cdef cppclass YBSchemaBuilder:

        YBColumnSpec* AddColumn(string& name)
        YBSchemaBuilder* SetPrimaryKey(vector[string]& key_col_names);

        Status Build(YBSchema* schema)


cdef extern from "yb/client/row_result.h" namespace "yb::client" nogil:

    cdef cppclass YBRowResult:
        c_bool IsNull(Slice& col_name)
        c_bool IsNull(int col_idx)

        # These getters return a bad Status if the type does not match,
        # the value is unset, or the value is NULL. Otherwise they return
        # the current set value in *val.
        Status GetBool(Slice& col_name, c_bool* val)

        Status GetInt8(Slice& col_name, int8_t* val)
        Status GetInt16(Slice& col_name, int16_t* val)
        Status GetInt32(Slice& col_name, int32_t* val)
        Status GetInt64(Slice& col_name, int64_t* val)

        Status GetTimestamp(const Slice& col_name,
                            int64_t* micros_since_utc_epoch)

        Status GetBool(int col_idx, c_bool* val)

        Status GetInt8(int col_idx, int8_t* val)
        Status GetInt16(int col_idx, int16_t* val)
        Status GetInt32(int col_idx, int32_t* val)
        Status GetInt64(int col_idx, int64_t* val)

        Status GetString(Slice& col_name, Slice* val)
        Status GetString(int col_idx, Slice* val)

        Status GetFloat(Slice& col_name, float* val)
        Status GetFloat(int col_idx, float* val)

        Status GetDouble(Slice& col_name, double* val)
        Status GetDouble(int col_idx, double* val)

        Status GetBinary(const Slice& col_name, Slice* val)
        Status GetBinary(int col_idx, Slice* val)

        const void* cell(int col_idx)
        string ToString()


cdef extern from "yb/util/slice.h" namespace "kudu" nogil:

    cdef cppclass Slice:
        Slice()
        Slice(const uint8_t* data, size_t n)
        Slice(const char* data, size_t n)

        Slice(string& s)
        Slice(const char* s)

        # Many other constructors have been omitted; we can return and add them
        # as needed for the code generation.

        const uint8_t* data()
        uint8_t* mutable_data()
        size_t size()
        c_bool empty()

        uint8_t operator[](size_t n)

        void clear()
        void remove_prefix(size_t n)
        void truncate(size_t n)

        Status check_size(size_t expected_size)

        string ToString()

        string ToDebugString()
        string ToDebugString(size_t max_len)

        int compare(Slice& b)

        c_bool starts_with(Slice& x)

        void relocate(uint8_t* d)

        # Many other API methods omitted


cdef extern from "yb/common/partial_row.h" namespace "kudu" nogil:

    cdef cppclass YBPartialRow:
        # Schema must not be garbage-collected
        # YBPartialRow(const Schema* schema)

        #----------------------------------------------------------------------
        # Setters

        # Slice setters
        Status SetBool(Slice& col_name, c_bool val)

        Status SetInt8(Slice& col_name, int8_t val)
        Status SetInt16(Slice& col_name, int16_t val)
        Status SetInt32(Slice& col_name, int32_t val)
        Status SetInt64(Slice& col_name, int64_t val)

        Status SetTimestamp(const Slice& col_name,
                            int64_t micros_since_utc_epoch)
        Status SetTimestamp(int col_idx, int64_t micros_since_utc_epoch)

        Status SetDouble(Slice& col_name, double val)
        Status SetFloat(Slice& col_name, float val)

        # Integer setters
        Status SetBool(int col_idx, c_bool val)

        Status SetInt8(int col_idx, int8_t val)
        Status SetInt16(int col_idx, int16_t val)
        Status SetInt32(int col_idx, int32_t val)
        Status SetInt64(int col_idx, int64_t val)

        Status SetDouble(int col_idx, double val)
        Status SetFloat(int col_idx, float val)

        # Set, but does not copy string
        Status SetString(Slice& col_name, Slice& val)
        Status SetString(int col_idx, Slice& val)

        Status SetStringCopy(Slice& col_name, Slice& val)
        Status SetStringCopy(int col_idx, Slice& val)

        Status SetBinaryCopy(const Slice& col_name, const Slice& val)
        Status SetBinaryCopy(int col_idx, const Slice& val)

        Status SetNull(Slice& col_name)
        Status SetNull(int col_idx)

        Status Unset(Slice& col_name)
        Status Unset(int col_idx)

        #----------------------------------------------------------------------
        # Getters

        c_bool IsColumnSet(Slice& col_name)
        c_bool IsColumnSet(int col_idx)

        c_bool IsNull(Slice& col_name)
        c_bool IsNull(int col_idx)

        Status GetBool(Slice& col_name, c_bool* val)
        Status GetBool(int col_idx, c_bool* val)

        Status GetInt8(Slice& col_name, int8_t* val)
        Status GetInt8(int col_idx, int8_t* val)

        Status GetInt16(Slice& col_name, int16_t* val)
        Status GetInt16(int col_idx, int16_t* val)

        Status GetInt32(Slice& col_name, int32_t* val)
        Status GetInt32(int col_idx, int32_t* val)

        Status GetInt64(Slice& col_name, int64_t* val)
        Status GetInt64(int col_idx, int64_t* val)

        Status GetTimestamp(const Slice& col_name,
                            int64_t* micros_since_utc_epoch)
        Status GetTimestamp(int col_idx, int64_t* micros_since_utc_epoch)

        Status GetDouble(Slice& col_name, double* val)
        Status GetDouble(int col_idx, double* val)

        Status GetFloat(Slice& col_name, float* val)
        Status GetFloat(int col_idx, float* val)

        # Gets the string but does not copy the value. Callers should
        # copy the resulting Slice if necessary.
        Status GetString(Slice& col_name, Slice* val)
        Status GetString(int col_idx, Slice* val)

        Status GetBinary(const Slice& col_name, Slice* val)
        Status GetBinary(int col_idx, Slice* val)

        Status EncodeRowKey(string* encoded_key)
        string ToEncodedRowKeyOrDie()

        # Return true if all of the key columns have been specified
        # for this mutation.
        c_bool IsKeySet()

        # Return true if all columns have been specified.
        c_bool AllColumnsSet()
        string ToString()

        # const Schema* schema()


cdef extern from "yb/client/write_op.h" namespace "yb::client" nogil:

    enum WriteType" yb::client::YBWriteOperation::Type":
        INSERT " yb::client::YBWriteOperation::INSERT"
        UPDATE " yb::client::YBWriteOperation::UPDATE"
        DELETE " yb::client::YBWriteOperation::DELETE"

    cdef cppclass YBWriteOperation:
        YBPartialRow& row()
        YBPartialRow* mutable_row()

        # This is a pure virtual function implemented on each of the cppclass
        # subclasses
        string ToString()

        # Also a pure virtual
        WriteType type()

    cdef cppclass YBInsert(YBWriteOperation):
        pass

    cdef cppclass YBDelete(YBWriteOperation):
        pass

    cdef cppclass YBUpdate(YBWriteOperation):
        pass


cdef extern from "yb/client/scan_predicate.h" namespace "yb::client" nogil:
    enum ComparisonOp" yb::client::YBPredicate::ComparisonOp":
        KUDU_LESS_EQUAL    " yb::client::YBPredicate::LESS_EQUAL"
        KUDU_GREATER_EQUAL " yb::client::YBPredicate::GREATER_EQUAL"
        KUDU_EQUAL         " yb::client::YBPredicate::EQUAL"

    cdef cppclass YBPredicate:
        YBPredicate* Clone()


cdef extern from "yb/client/value.h" namespace "yb::client" nogil:

    cdef cppclass YBValue:
        @staticmethod
        YBValue* FromInt(int64_t val);

        @staticmethod
        YBValue* FromFloat(float val);

        @staticmethod
        YBValue* FromDouble(double val);

        @staticmethod
        YBValue* FromBool(c_bool val);

        @staticmethod
        YBValue* CopyString(const Slice& s);


cdef extern from "yb/client/client.h" namespace "yb::client" nogil:

    # Omitted YBClient::ReplicaSelection enum

    cdef cppclass YBClient:

        Status DeleteTable(const string& table_name)
        Status OpenTable(const string& table_name,
                         shared_ptr[YBTable]* table)
        Status GetTableSchema(const string& table_name, YBSchema* schema)

        YBTableCreator* NewTableCreator()
        Status IsCreateTableInProgress(const string& table_name,
                                       c_bool* create_in_progress)

        c_bool IsMultiMaster()

        Status ListTables(vector[string]* tables)
        Status ListTables(vector[string]* tables, const string& filter)

        Status TableExists(const string& table_name, c_bool* exists)

        YBTableAlterer* NewTableAlterer()
        Status IsAlterTableInProgress(const string& table_name,
                                      c_bool* alter_in_progress)

        shared_ptr[YBSession] NewSession()

    cdef cppclass YBClientBuilder:
        YBClientBuilder()
        YBClientBuilder& master_server_addrs(const vector[string]& addrs)
        YBClientBuilder& add_master_server_addr(const string& addr)

        YBClientBuilder& default_admin_operation_timeout(
            const MonoDelta& timeout)

        YBClientBuilder& default_rpc_timeout(const MonoDelta& timeout)

        Status Build(shared_ptr[YBClient]* client)

    cdef cppclass YBTableCreator:
        YBTableCreator& table_name(string& name)
        YBTableCreator& schema(YBSchema* schema)
        YBTableCreator& split_keys(vector[string]& keys)
        YBTableCreator& num_replicas(int n_replicas)
        YBTableCreator& wait(c_bool wait)

        Status Create()

    cdef cppclass YBTableAlterer:
        # The name of the existing table to alter
        YBTableAlterer& table_name(string& name)

        YBTableAlterer& rename_table(string& name)

        YBTableAlterer& add_column(string& name, DataType type,
                                     const void *default_value)
        YBTableAlterer& add_column(string& name, DataType type,
                                     const void *default_value,
                                     YBColumnStorageAttributes attr)

        YBTableAlterer& add_nullable_column(string& name, DataType type)

        YBTableAlterer& drop_column(string& name)

        YBTableAlterer& rename_column(string& old_name, string& new_name)

        YBTableAlterer& wait(c_bool wait)

        Status Alter()

    # Instances of YBTable are not directly instantiated by users of the
    # client.
    cdef cppclass YBTable:

        string& name()
        YBSchema& schema()

        YBInsert* NewInsert()
        YBUpdate* NewUpdate()
        YBDelete* NewDelete()

        YBPredicate* NewComparisonPredicate(const Slice& col_name,
                                              ComparisonOp op,
                                              YBValue* value);

        YBClient* client()
        # const PartitionSchema& partition_schema()

    enum FlushMode" yb::client::YBSession::FlushMode":
        FlushMode_AutoSync " yb::client::YBSession::AUTO_FLUSH_SYNC"
        FlushMode_AutoBackground " yb::client::YBSession::AUTO_FLUSH_BACKGROUND"
        FlushMode_Manual " yb::client::YBSession::MANUAL_FLUSH"

    cdef cppclass YBSession:

        Status SetFlushMode(FlushMode m)

        void SetMutationBufferSpace(size_t size)
        void SetTimeoutMillis(int millis)

        void SetPriority(int priority)

        Status Apply(YBWriteOperation* write_op)
        Status Apply(YBInsert* write_op)
        Status Apply(YBUpdate* write_op)
        Status Apply(YBDelete* write_op)

        # This is thread-safe
        Status Flush()

        # TODO: Will need to decide on a strategy for exposing the session's
        # async API to Python

        # Status ApplyAsync(YBWriteOperation* write_op,
        #                   YBStatusCallback cb)
        # Status ApplyAsync(YBInsert* write_op,
        #                   YBStatusCallback cb)
        # Status ApplyAsync(YBUpdate* write_op,
        #                   YBStatusCallback cb)
        # Status ApplyAsync(YBDelete* write_op,
        #                   YBStatusCallback cb)
        # void FlushAsync(YBStatusCallback& cb)


        Status Close()
        c_bool HasPendingOperations()
        int CountBufferedOperations()

        int CountPendingErrors()
        void GetPendingErrors(vector[C_YBError*]* errors, c_bool* overflowed)

        YBClient* client()

    enum ReadMode" yb::client::YBScanner::ReadMode":
        READ_LATEST " yb::client::YBScanner::READ_LATEST"
        READ_AT_SNAPSHOT " yb::client::YBScanner::READ_AT_SNAPSHOT"

    cdef cppclass YBScanner:
        YBScanner(YBTable* table)

        Status AddConjunctPredicate(YBPredicate* pred)

        Status Open()
        void Close()

        c_bool HasMoreRows()
        Status NextBatch(vector[YBRowResult]* rows)
        Status SetBatchSizeBytes(uint32_t batch_size)

        # Pending definition of ReplicaSelection enum
        # Status SetSelection(ReplicaSelection selection)

        Status SetReadMode(ReadMode read_mode)
        Status SetSnapshot(uint64_t snapshot_timestamp_micros)
        Status SetTimeoutMillis(int millis)
        Status SetFaultTolerant()

        string ToString()

    cdef cppclass C_YBError " yb::client::YBError":

        Status& status()

        YBWriteOperation& failed_op()
        YBWriteOperation* release_failed_op()

        c_bool was_possibly_successful()

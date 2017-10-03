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

cdef extern from "kudu/client/shared_ptr.h" namespace "kudu::client::sp" nogil:

    cdef cppclass shared_ptr[T]:
        T* get()
        void reset()
        void reset(T* p)

cdef extern from "kudu/util/status.h" namespace "kudu" nogil:

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


cdef extern from "kudu/util/monotime.h" namespace "kudu" nogil:

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


cdef extern from "kudu/client/schema.h" namespace "kudu::client" nogil:

    enum DataType" kudu::client::KuduColumnSchema::DataType":
        KUDU_INT8 " kudu::client::KuduColumnSchema::INT8"
        KUDU_INT16 " kudu::client::KuduColumnSchema::INT16"
        KUDU_INT32 " kudu::client::KuduColumnSchema::INT32"
        KUDU_INT64 " kudu::client::KuduColumnSchema::INT64"
        KUDU_STRING " kudu::client::KuduColumnSchema::STRING"
        KUDU_BOOL " kudu::client::KuduColumnSchema::BOOL"
        KUDU_FLOAT " kudu::client::KuduColumnSchema::FLOAT"
        KUDU_DOUBLE " kudu::client::KuduColumnSchema::DOUBLE"
        KUDU_BINARY " kudu::client::KuduColumnSchema::BINARY"
        KUDU_TIMESTAMP " kudu::client::KuduColumnSchema::TIMESTAMP"

    enum EncodingType" kudu::client::KuduColumnStorageAttributes::EncodingType":
        EncodingType_AUTO " kudu::client::KuduColumnStorageAttributes::AUTO_ENCODING"
        EncodingType_PLAIN " kudu::client::KuduColumnStorageAttributes::PLAIN_ENCODING"
        EncodingType_PREFIX " kudu::client::KuduColumnStorageAttributes::PREFIX_ENCODING"
        EncodingType_GROUP_VARINT " kudu::client::KuduColumnStorageAttributes::GROUP_VARINT"
        EncodingType_RLE " kudu::client::KuduColumnStorageAttributes::RLE"

    enum CompressionType" kudu::client::KuduColumnStorageAttributes::CompressionType":
        CompressionType_DEFAULT " kudu::client::KuduColumnStorageAttributes::DEFAULT_COMPRESSION"
        CompressionType_NONE " kudu::client::KuduColumnStorageAttributes::NO_COMPRESSION"
        CompressionType_SNAPPY " kudu::client::KuduColumnStorageAttributes::SNAPPY"
        CompressionType_LZ4 " kudu::client::KuduColumnStorageAttributes::LZ4"
        CompressionType_ZLIB " kudu::client::KuduColumnStorageAttributes::ZLIB"

    cdef struct KuduColumnStorageAttributes:
        KuduColumnStorageAttributes()

        EncodingType encoding
        CompressionType compression
        string ToString()

    cdef cppclass KuduColumnSchema:
        KuduColumnSchema(const KuduColumnSchema& other)
        KuduColumnSchema(const string& name, DataType type)
        KuduColumnSchema(const string& name, DataType type, c_bool is_nullable)
        KuduColumnSchema(const string& name, DataType type, c_bool is_nullable,
                         const void* default_value)

        string& name()
        c_bool is_nullable()
        DataType type()

        c_bool Equals(KuduColumnSchema& other)
        void CopyFrom(KuduColumnSchema& other)

    cdef cppclass KuduSchema:
        KuduSchema()
        KuduSchema(vector[KuduColumnSchema]& columns, int key_columns)

        c_bool Equals(const KuduSchema& other)
        KuduColumnSchema Column(size_t idx)
        size_t num_columns()

        void GetPrimaryKeyColumnIndexes(vector[int]* indexes)

        KuduPartialRow* NewRow()

    cdef cppclass KuduColumnSpec:

         KuduColumnSpec* Default(KuduValue* value)
         KuduColumnSpec* RemoveDefault()

         KuduColumnSpec* Compression(CompressionType compression)
         KuduColumnSpec* Encoding(EncodingType encoding)
         KuduColumnSpec* BlockSize(int32_t block_size)

         KuduColumnSpec* PrimaryKey()
         KuduColumnSpec* NotNull()
         KuduColumnSpec* Nullable()
         KuduColumnSpec* Type(DataType type_)

         KuduColumnSpec* RenameTo(string& new_name)


    cdef cppclass KuduSchemaBuilder:

        KuduColumnSpec* AddColumn(string& name)
        KuduSchemaBuilder* SetPrimaryKey(vector[string]& key_col_names);

        Status Build(KuduSchema* schema)


cdef extern from "kudu/client/row_result.h" namespace "kudu::client" nogil:

    cdef cppclass KuduRowResult:
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


cdef extern from "kudu/util/slice.h" namespace "kudu" nogil:

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


cdef extern from "kudu/common/partial_row.h" namespace "kudu" nogil:

    cdef cppclass KuduPartialRow:
        # Schema must not be garbage-collected
        # KuduPartialRow(const Schema* schema)

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


cdef extern from "kudu/client/write_op.h" namespace "kudu::client" nogil:

    enum WriteType" kudu::client::KuduWriteOperation::Type":
        INSERT " kudu::client::KuduWriteOperation::INSERT"
        UPDATE " kudu::client::KuduWriteOperation::UPDATE"
        DELETE " kudu::client::KuduWriteOperation::DELETE"

    cdef cppclass KuduWriteOperation:
        KuduPartialRow& row()
        KuduPartialRow* mutable_row()

        # This is a pure virtual function implemented on each of the cppclass
        # subclasses
        string ToString()

        # Also a pure virtual
        WriteType type()

    cdef cppclass KuduInsert(KuduWriteOperation):
        pass

    cdef cppclass KuduDelete(KuduWriteOperation):
        pass

    cdef cppclass KuduUpdate(KuduWriteOperation):
        pass


cdef extern from "kudu/client/scan_predicate.h" namespace "kudu::client" nogil:
    enum ComparisonOp" kudu::client::KuduPredicate::ComparisonOp":
        KUDU_LESS_EQUAL    " kudu::client::KuduPredicate::LESS_EQUAL"
        KUDU_GREATER_EQUAL " kudu::client::KuduPredicate::GREATER_EQUAL"
        KUDU_EQUAL         " kudu::client::KuduPredicate::EQUAL"

    cdef cppclass KuduPredicate:
        KuduPredicate* Clone()


cdef extern from "kudu/client/value.h" namespace "kudu::client" nogil:

    cdef cppclass KuduValue:
        @staticmethod
        KuduValue* FromInt(int64_t val);

        @staticmethod
        KuduValue* FromFloat(float val);

        @staticmethod
        KuduValue* FromDouble(double val);

        @staticmethod
        KuduValue* FromBool(c_bool val);

        @staticmethod
        KuduValue* CopyString(const Slice& s);


cdef extern from "kudu/client/client.h" namespace "kudu::client" nogil:

    # Omitted KuduClient::ReplicaSelection enum

    cdef cppclass KuduClient:

        Status DeleteTable(const string& table_name)
        Status OpenTable(const string& table_name,
                         shared_ptr[KuduTable]* table)
        Status GetTableSchema(const string& table_name, KuduSchema* schema)

        KuduTableCreator* NewTableCreator()
        Status IsCreateTableInProgress(const string& table_name,
                                       c_bool* create_in_progress)

        c_bool IsMultiMaster()

        Status ListTables(vector[string]* tables)
        Status ListTables(vector[string]* tables, const string& filter)

        Status TableExists(const string& table_name, c_bool* exists)

        KuduTableAlterer* NewTableAlterer()
        Status IsAlterTableInProgress(const string& table_name,
                                      c_bool* alter_in_progress)

        shared_ptr[KuduSession] NewSession()

    cdef cppclass KuduClientBuilder:
        KuduClientBuilder()
        KuduClientBuilder& master_server_addrs(const vector[string]& addrs)
        KuduClientBuilder& add_master_server_addr(const string& addr)

        KuduClientBuilder& default_admin_operation_timeout(
            const MonoDelta& timeout)

        KuduClientBuilder& default_rpc_timeout(const MonoDelta& timeout)

        Status Build(shared_ptr[KuduClient]* client)

    cdef cppclass KuduTableCreator:
        KuduTableCreator& table_name(string& name)
        KuduTableCreator& schema(KuduSchema* schema)
        KuduTableCreator& split_keys(vector[string]& keys)
        KuduTableCreator& num_replicas(int n_replicas)
        KuduTableCreator& wait(c_bool wait)

        Status Create()

    cdef cppclass KuduTableAlterer:
        # The name of the existing table to alter
        KuduTableAlterer& table_name(string& name)

        KuduTableAlterer& rename_table(string& name)

        KuduTableAlterer& add_column(string& name, DataType type,
                                     const void *default_value)
        KuduTableAlterer& add_column(string& name, DataType type,
                                     const void *default_value,
                                     KuduColumnStorageAttributes attr)

        KuduTableAlterer& add_nullable_column(string& name, DataType type)

        KuduTableAlterer& drop_column(string& name)

        KuduTableAlterer& rename_column(string& old_name, string& new_name)

        KuduTableAlterer& wait(c_bool wait)

        Status Alter()

    # Instances of KuduTable are not directly instantiated by users of the
    # client.
    cdef cppclass KuduTable:

        string& name()
        KuduSchema& schema()

        KuduInsert* NewInsert()
        KuduUpdate* NewUpdate()
        KuduDelete* NewDelete()

        KuduPredicate* NewComparisonPredicate(const Slice& col_name,
                                              ComparisonOp op,
                                              KuduValue* value);

        KuduClient* client()
        # const PartitionSchema& partition_schema()

    enum FlushMode" kudu::client::KuduSession::FlushMode":
        FlushMode_AutoSync " kudu::client::KuduSession::AUTO_FLUSH_SYNC"
        FlushMode_AutoBackground " kudu::client::KuduSession::AUTO_FLUSH_BACKGROUND"
        FlushMode_Manual " kudu::client::KuduSession::MANUAL_FLUSH"

    cdef cppclass KuduSession:

        Status SetFlushMode(FlushMode m)

        void SetMutationBufferSpace(size_t size)
        void SetTimeoutMillis(int millis)

        void SetPriority(int priority)

        Status Apply(KuduWriteOperation* write_op)
        Status Apply(KuduInsert* write_op)
        Status Apply(KuduUpdate* write_op)
        Status Apply(KuduDelete* write_op)

        # This is thread-safe
        Status Flush()

        # TODO: Will need to decide on a strategy for exposing the session's
        # async API to Python

        # Status ApplyAsync(KuduWriteOperation* write_op,
        #                   KuduStatusCallback cb)
        # Status ApplyAsync(KuduInsert* write_op,
        #                   KuduStatusCallback cb)
        # Status ApplyAsync(KuduUpdate* write_op,
        #                   KuduStatusCallback cb)
        # Status ApplyAsync(KuduDelete* write_op,
        #                   KuduStatusCallback cb)
        # void FlushAsync(KuduStatusCallback& cb)


        Status Close()
        c_bool HasPendingOperations()
        int CountBufferedOperations()

        int CountPendingErrors()
        void GetPendingErrors(vector[C_KuduError*]* errors, c_bool* overflowed)

        KuduClient* client()

    enum ReadMode" kudu::client::KuduScanner::ReadMode":
        READ_LATEST " kudu::client::KuduScanner::READ_LATEST"
        READ_AT_SNAPSHOT " kudu::client::KuduScanner::READ_AT_SNAPSHOT"

    cdef cppclass KuduScanner:
        KuduScanner(KuduTable* table)

        Status AddConjunctPredicate(KuduPredicate* pred)

        Status Open()
        void Close()

        c_bool HasMoreRows()
        Status NextBatch(vector[KuduRowResult]* rows)
        Status SetBatchSizeBytes(uint32_t batch_size)

        # Pending definition of ReplicaSelection enum
        # Status SetSelection(ReplicaSelection selection)

        Status SetReadMode(ReadMode read_mode)
        Status SetSnapshot(uint64_t snapshot_timestamp_micros)
        Status SetTimeoutMillis(int millis)
        Status SetFaultTolerant()

        string ToString()

    cdef cppclass C_KuduError " kudu::client::KuduError":

        Status& status()

        KuduWriteOperation& failed_op()
        KuduWriteOperation* release_failed_op()

        c_bool was_possibly_successful()

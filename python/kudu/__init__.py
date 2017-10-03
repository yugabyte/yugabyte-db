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

from kudu.client import (Client, Table, Scanner, Session,  # noqa
                         Insert, Update, Delete, Predicate,
                         TimeDelta, KuduError,
                         FLUSH_AUTO_BACKGROUND,
                         FLUSH_AUTO_SYNC,
                         FLUSH_MANUAL)

from kudu.errors import (KuduException, KuduBadStatus, KuduNotFound,  # noqa
                         KuduNotSupported,
                         KuduInvalidArgument)

from kudu.schema import (int8, int16, int32, int64, string_ as string,  # noqa
                         double_ as double, float_, binary,
                         timestamp,
                         KuduType,
                         SchemaBuilder, ColumnSpec, Schema, ColumnSchema,
                         COMPRESSION_DEFAULT,
                         COMPRESSION_NONE,
                         COMPRESSION_SNAPPY,
                         COMPRESSION_LZ4,
                         COMPRESSION_ZLIB,
                         ENCODING_AUTO,
                         ENCODING_PLAIN,
                         ENCODING_PREFIX,
                         ENCODING_GROUP_VARINT,
                         ENCODING_RLE)


def connect(host, port, admin_timeout_ms=None, rpc_timeout_ms=None):
    """
    Connect to a Kudu master server

    Parameters
    ----------
    host : string
      Server address of master
    port : int
      Server port
    admin_timeout_ms : int, optional
      Admin timeout in milliseconds
    rpc_timeout_ms : int, optional
      RPC timeout in milliseconds

    Returns
    -------
    client : kudu.Client
    """
    addr = '{0}:{1}'.format(host, port)
    return Client(addr, admin_timeout_ms=admin_timeout_ms,
                  rpc_timeout_ms=rpc_timeout_ms)


def timedelta(seconds=0, millis=0, micros=0, nanos=0):
    """
    Construct a Kudu TimeDelta to set timeouts, etc. Use this function instead
    of interacting with the TimeDelta class yourself.

    Returns
    -------
    delta : kudu.client.TimeDelta
    """
    from kudu.compat import long
    # TimeDelta is a wrapper for kudu::MonoDelta
    total_ns = (long(0) + seconds * long(1000000000) +
                millis * long(1000000) + micros * long(1000) + nanos)
    return TimeDelta.from_nanos(total_ns)


def schema_builder():
    """
    Create a kudu.SchemaBuilder instance

    Examples
    --------
    builder = kudu.schema_builder()
    builder.add_column('key1', kudu.int64, nullable=False)
    builder.add_column('key2', kudu.int32, nullable=False)

    (builder.add_column('name', kudu.string)
     .nullable()
     .compression('lz4'))

    builder.add_column('value1', kudu.double)
    builder.add_column('value2', kudu.int8, encoding='rle')
    builder.set_primary_keys(['key1', 'key2'])

    schema = builder.build()

    Returns
    -------
    builder : SchemaBuilder
    """
    return SchemaBuilder()


from .version import version as __version__  # noqa

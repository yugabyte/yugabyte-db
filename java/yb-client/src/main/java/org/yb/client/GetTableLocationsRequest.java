// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import com.google.protobuf.UnsafeByteOperations;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClientOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

/**
 * Package-private RPC that can only go to a master.
 */
@InterfaceAudience.Private
class GetTableLocationsRequest extends YRpc<MasterClientOuterClass.GetTableLocationsResponsePB> {

  private final byte[] startPartitionKey;
  private final byte[] endKey;
  private final String tableId;
  private final int maxTablets;
  private final boolean includeInactive;

  GetTableLocationsRequest(YBTable table, byte[] startPartitionKey,
                           byte[] endPartitionKey, String tableId, int maxTablets) {
    this(table, startPartitionKey, endPartitionKey, tableId, maxTablets, false);
  }

  GetTableLocationsRequest(YBTable table, byte[] startPartitionKey,
                           byte[] endPartitionKey, String tableId, int maxTablets,
                           boolean includeInactive) {
    super(table);
    if (startPartitionKey != null && endPartitionKey != null
        && Bytes.memcmp(startPartitionKey, endPartitionKey) > 0) {
      throw new IllegalArgumentException(
          "The start partition key must be smaller or equal to the end partition key");
    }
    this.startPartitionKey = startPartitionKey;
    this.endKey = endPartitionKey;
    this.tableId = tableId;
    this.maxTablets = maxTablets;
    this.includeInactive = includeInactive;
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "GetTableLocations";
  }

  @Override
  Pair<MasterClientOuterClass.GetTableLocationsResponsePB, Object> deserialize(
      final CallResponse callResponse, String tsUUID)
      throws Exception {
    MasterClientOuterClass.GetTableLocationsResponsePB.Builder builder =
        MasterClientOuterClass.GetTableLocationsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    MasterClientOuterClass.GetTableLocationsResponsePB resp = builder.build();
    return new Pair<MasterClientOuterClass.GetTableLocationsResponsePB, Object>(
        resp, builder.hasError() ? builder.getError() : null);
  }

  @Override
  ByteBuf serialize(Message header) {
    final MasterClientOuterClass.GetTableLocationsRequestPB.Builder builder = MasterClientOuterClass
        .GetTableLocationsRequestPB.newBuilder();
    builder.setTable(MasterTypes.TableIdentifierPB.newBuilder().
        setTableId(ByteString.copyFromUtf8(tableId)));

    if (maxTablets != 0) {
      builder.setMaxReturnedLocations(maxTablets);
    }

    builder.setIncludeInactive(includeInactive);

    if (startPartitionKey != null) {
      builder.setPartitionKeyStart(UnsafeByteOperations.unsafeWrap(startPartitionKey));
    }
    if (endKey != null) {
      builder.setPartitionKeyEnd(UnsafeByteOperations.unsafeWrap(endKey));
    }
    return toChannelBuffer(header, builder.build());
  }
}

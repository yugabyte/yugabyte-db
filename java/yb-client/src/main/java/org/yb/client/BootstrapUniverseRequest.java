// Copyright (c) YugaByte, Inc.
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
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

import java.util.List;
import java.util.stream.Collectors;

public class BootstrapUniverseRequest extends YRpc<BootstrapUniverseResponse> {

  private final List<String> tableIDs;

  BootstrapUniverseRequest(YBTable table, List<String> tableIDs) {
    super(table);
    this.tableIDs = tableIDs;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final CdcService.BootstrapProducerRequestPB.Builder builder =
      CdcService.BootstrapProducerRequestPB.newBuilder()
      .addAllTableIds(tableIDs);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "BootstrapUniverse";
  }

  @Override
  Pair<BootstrapUniverseResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final CdcService.BootstrapProducerResponsePB.Builder builder =
      CdcService.BootstrapProducerResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final CdcService.CDCErrorPB error = builder.hasError() ? builder.getError() : null;
    final List<String> bootstrapIDs = builder
      .getCdcBootstrapIdsList()
      .stream()
      .map(ByteString::toString)
      .collect(Collectors.toList());

    BootstrapUniverseResponse response =
      new BootstrapUniverseResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error, bootstrapIDs);

    return new Pair<>(response, error);
  }
}

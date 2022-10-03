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
import io.netty.buffer.ByteBuf;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

import java.util.List;
import java.util.stream.Collectors;

public class BootstrapUniverseRequest extends YRpc<BootstrapUniverseResponse> {

  private final List<String> tableIds;

  BootstrapUniverseRequest(YBTable table, List<String> tableIds) {
    super(table);
    this.tableIds = tableIds;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final CdcService.BootstrapProducerRequestPB.Builder builder =
      CdcService.BootstrapProducerRequestPB.newBuilder()
      .addAllTableIds(tableIds);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return CDC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "BootstrapProducer";
  }

  @Override
  Pair<BootstrapUniverseResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final CdcService.BootstrapProducerResponsePB.Builder builder =
      CdcService.BootstrapProducerResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final CdcService.CDCErrorPB error = builder.hasError() ? builder.getError() : null;
    final List<String> bootstrapIds = builder
      .getCdcBootstrapIdsList()
      .stream()
      .map(ByteString::toStringUtf8)
      .collect(Collectors.toList());

    BootstrapUniverseResponse response =
      new BootstrapUniverseResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error, bootstrapIds);

    return new Pair<>(response, error);
  }
}

// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetNamespaceInfoRequest extends YRpc<GetNamespaceInfoResponse> {

  static final String GET_NAMESPACE_INFO = "GetNamespaceInfo";

  private final YQLDatabase databaseType;

  private final String namespaceName;

  GetNamespaceInfoRequest(YBTable masterTable, String namespaceName, YQLDatabase databaseType) {
    super(masterTable);
    this.namespaceName = namespaceName;
    this.databaseType = databaseType;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterDdlOuterClass.GetNamespaceInfoRequestPB.Builder builder =
        MasterDdlOuterClass.GetNamespaceInfoRequestPB.newBuilder();
    builder.setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder()
           .setName(this.namespaceName)
           .setDatabaseType(this.databaseType));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return GET_NAMESPACE_INFO;
  }

  @Override
  Pair<GetNamespaceInfoResponse, Object> deserialize(CallResponse callResponse,
                                                    String masterUUID) throws Exception {
    final MasterDdlOuterClass.GetNamespaceInfoResponsePB.Builder respBuilder =
        MasterDdlOuterClass.GetNamespaceInfoResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    MasterTypes.MasterErrorPB err = respBuilder.getError();
    GetNamespaceInfoResponse response = new GetNamespaceInfoResponse(
        deadlineTracker.getElapsedMillis(), masterUUID,
        respBuilder.getNamespace(), hasErr ? err : null);
    return new Pair<GetNamespaceInfoResponse, Object>(response, hasErr ? err : null);
  }
}

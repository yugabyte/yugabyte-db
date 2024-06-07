package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.master.MasterDdlOuterClass.ListNamespacesRequestPB;
import org.yb.master.MasterDdlOuterClass.ListNamespacesResponsePB;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class ListNamespacesRequest extends YRpc<ListNamespacesResponse> {

    private YQLDatabase databaseType;

    ListNamespacesRequest(YBTable table, YQLDatabase databaseType) {
        super(table);
        this.databaseType = databaseType;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final ListNamespacesRequestPB namespaceRequest =
                ListNamespacesRequestPB.newBuilder().setDatabaseType(this.databaseType).build();
        return toChannelBuffer(header, namespaceRequest);

    }

    @Override
    String serviceName() { return MASTER_SERVICE_NAME; }

    @Override
    String method() {
        return "ListNamespaces";
    }

    @Override
    Pair<ListNamespacesResponse, Object> deserialize(CallResponse callResponse,
                                               String masterUUID) throws Exception {
        final ListNamespacesResponsePB.Builder respBuilder = ListNamespacesResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                    respBuilder.hasError() ? respBuilder.getError() : null;
        ListNamespacesResponse response =
                new ListNamespacesResponse(deadlineTracker.getElapsedMillis(),
                                        masterUUID, serverError, respBuilder.getNamespacesList());
        return new Pair<ListNamespacesResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
    }
}

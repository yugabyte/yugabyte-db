package org.yb.client;

import org.yb.server.ServerBase.ReloadCertificatesRequestPB;
import org.yb.server.ServerBase.ReloadCertificatesResponsePB;
import org.yb.server.ServerBase.ReloadCertificatesResponsePB.Builder;
import org.yb.util.Pair;

import com.google.common.net.HostAndPort;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

public class ReloadCertificateRequest extends YRpc<ReloadCertificateResponse> {

    private final String nodeAddress; // host:port

    /**
     *
     * @param table
     * @param nodeAddress host:port of the db node
     */
    public ReloadCertificateRequest(YBTable table, HostAndPort nodeAddress) {
        super(table);
        this.nodeAddress = nodeAddress.toString();
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        return toChannelBuffer(header,
                ReloadCertificatesRequestPB.newBuilder().build());
    }

    @Override
    String serviceName() {
        return GENERIC_SERVICE_NAME;
    }

    @Override
    String method() {
        return "ReloadCertificates";
    }

    @Override
    Pair<ReloadCertificateResponse, Object> deserialize(
            CallResponse callResponse, String tsUUID) throws Exception {

        Builder responseBuilder = ReloadCertificatesResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), responseBuilder);

        ReloadCertificateResponse response = new ReloadCertificateResponse(
                this.nodeAddress, deadlineTracker.getElapsedMillis(), tsUUID);
        return new Pair<ReloadCertificateResponse, Object>(response, null);
    }

    public String getNodeAddress() {
        return nodeAddress;
    }

}

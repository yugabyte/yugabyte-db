package org.yb.client;

public class ReloadCertificateResponse extends YRpcResponse {

    private final String nodeAddress;

    ReloadCertificateResponse(String nodeAddress, long elapsedMillis, String tsUUID) {
        super(elapsedMillis, tsUUID);
        this.nodeAddress = nodeAddress;
    }

    public String getNodeAddress() {
        return nodeAddress;
    }

}

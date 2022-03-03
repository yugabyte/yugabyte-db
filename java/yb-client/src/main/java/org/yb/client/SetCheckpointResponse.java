package org.yb.client;

public class SetCheckpointResponse extends YRpcResponse {
  public SetCheckpointResponse(long elapsedMillis, String tsUUID) {
    super(elapsedMillis, tsUUID);
  }

}

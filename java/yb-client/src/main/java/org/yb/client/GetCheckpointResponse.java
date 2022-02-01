package org.yb.client;

public class GetCheckpointResponse extends YRpcResponse {

  private long index;
  private long term;

  public GetCheckpointResponse(long elapsedMillis, String uuid, long index, long term) {
    super(elapsedMillis, uuid);
    this.index = index;
    this.term = term;
  }

  public long getIndex() {
    return index;
  }

  public long getTerm() {
    return term;
  }
}

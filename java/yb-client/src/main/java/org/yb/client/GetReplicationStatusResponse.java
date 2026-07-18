package org.yb.client;


import java.util.List;
import org.yb.master.MasterReplicationOuterClass.ReplicationStatusPB;
import org.yb.master.MasterTypes.MasterErrorPB;

public class GetReplicationStatusResponse extends YRpcResponse {
  private final MasterErrorPB error;
  private final List<ReplicationStatusPB> statuses;

  public GetReplicationStatusResponse(
    long elapsedMillis, String tsUUID, MasterErrorPB error, List<ReplicationStatusPB> statuses) {
    super(elapsedMillis, tsUUID);
    this.error = error;
    this.statuses = statuses;
  }

  public boolean hasError() {
    return error != null;
  }

  public String errorMessage() {
    if (error == null) {
      return "";
    }

    return error.getStatus().getMessage();
  }

  public List<ReplicationStatusPB> getStatuses() {
    return statuses;
  }
}

package org.yb.client;


import java.util.List;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import org.yb.master.MasterReplicationOuterClass.WaitForReplicationDrainResponsePB.UndrainedStreamInfoPB;
import org.yb.master.MasterTypes.MasterErrorPB;

public class WaitForReplicationDrainResponse extends YRpcResponse {
  private final MasterErrorPB error;
  private final List<UndrainedStreamInfoPB> undrainedStreams;

  public WaitForReplicationDrainResponse(
      long elapsedMillis, String tsUUID,
      MasterErrorPB error,
      List<UndrainedStreamInfoPB> undrainedStreams) {
    super(elapsedMillis, tsUUID);
    this.error = error;
    this.undrainedStreams = undrainedStreams;
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

  public List<UndrainedStreamInfoPB> getUndrainedStreams() {
    return undrainedStreams;
  }
}

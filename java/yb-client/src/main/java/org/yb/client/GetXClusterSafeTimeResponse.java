package org.yb.client;


import java.util.List;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import org.yb.master.MasterTypes.MasterErrorPB;

public class GetXClusterSafeTimeResponse extends YRpcResponse {
  private final MasterErrorPB error;
  private final List<NamespaceSafeTimePB> namespaceSafeTimes;

  public GetXClusterSafeTimeResponse(
      long elapsedMillis, String tsUUID,
      MasterErrorPB error,
      List<NamespaceSafeTimePB> namespaceSafeTimes) {
    super(elapsedMillis, tsUUID);
    this.error = error;
    this.namespaceSafeTimes = namespaceSafeTimes;
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

  public List<NamespaceSafeTimePB> getSafeTimes() {
    return namespaceSafeTimes;
  }
}

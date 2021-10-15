package org.yb.client;

import org.yb.WireProtocol.AppStatusPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;
import org.yb.master.Master.MasterErrorPB;

@InterfaceAudience.Public
public class IsSetupUniverseReplicationDoneResponse extends YRpcResponse {
  private final Master.MasterErrorPB serverError;
  private final boolean done;
  private final AppStatusPB replicationError;

  public IsSetupUniverseReplicationDoneResponse(
    long elapsedMillis,
    String tsUUID,
    Master.MasterErrorPB serverError,
    boolean done,
    AppStatusPB replicationError) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.done = done;
    this.replicationError = replicationError;
  }

  public boolean hasError() {
    return this.serverError != null;
  }

  public MasterErrorPB getError() {
    return this.serverError;
  }

  public boolean isDone() {
    return this.done;
  }

  public boolean hasReplicationError() {
    return this.replicationError != null;
  }

  public AppStatusPB getReplicationError() {
    return this.replicationError;
  }
}

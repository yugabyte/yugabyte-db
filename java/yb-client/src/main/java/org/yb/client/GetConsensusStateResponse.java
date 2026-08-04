// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.consensus.ConsensusTypes.LeaderLeaseStatus;
import org.yb.consensus.Metadata.ConsensusStatePB;
import org.yb.tserver.TserverTypes.TabletServerErrorPB;

@InterfaceAudience.Public
public class GetConsensusStateResponse extends YRpcResponse {

  ConsensusStatePB cstate;
  TabletServerErrorPB error;
  LeaderLeaseStatus leaderLeaseStatus;

  public GetConsensusStateResponse(long ellapsedMillis, String uuid,
      ConsensusStatePB cstate, TabletServerErrorPB error,
      LeaderLeaseStatus lls) {
    super(ellapsedMillis, uuid);
    this.cstate = cstate;
    this.error = error;
    this.leaderLeaseStatus = lls;
  }

  public ConsensusStatePB getConsensusState() {
    return cstate;
  }

  public boolean hasError() {
    return (error != null && !error.getStatus().getMessage().isEmpty());
  }

  public String errorMessage() {
    if (error == null) {
      return "";
    }
    return error.getStatus().getMessage();
  }

  public LeaderLeaseStatus getLeaderLeaseStatus() {
    return leaderLeaseStatus;
  }
}

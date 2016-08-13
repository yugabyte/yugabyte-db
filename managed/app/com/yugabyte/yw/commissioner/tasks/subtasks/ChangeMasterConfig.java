// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.IsLeaderReadyForChangeConfigResponse;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class ChangeMasterConfig extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ChangeMasterConfig.class);

  // The YB client to use.
  public YBClientService ybService;

  // Sleep time in each iteration while trying to check on master leader.
  public final long PER_ATTEMPT_SLEEP_TIME_MS = 100;

  // Create an enum specifying the operation type.
  public enum OpType {
    AddMaster,
    RemoveMaster
  }

  // Parameters for change master config task.
  public static class Params extends NodeTaskParams {
    // When true, the master hostPort is added to the current universe's quorum, otherwise it is
    // deleted.
    public OpType opType;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().nodeName +
           ", " + taskParams().opType.toString() + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  // Waits and check's if the leader master is ready to serve config changes.
  // If it times out, we return false.
  private boolean waitForLeaderReadyToDoChangeConfig(YBClient client, long timeoutMs) throws Exception {
    IsLeaderReadyForChangeConfigResponse readyResp;
    long start = System.currentTimeMillis();
    long now = -1;
    do {
      if (now > 0) {
        Thread.sleep(PER_ATTEMPT_SLEEP_TIME_MS);
      }
      now = System.currentTimeMillis();
      readyResp = client.isMasterLeaderReadyForChangeConfig();
      if (readyResp.hasError()) {
        LOG.error("Leader Ready check returned  error : {}.", readyResp.errorMessage());
        return false;
      }

      // If 'now' regresses due to some timing system issues, then fail fast.
      if (now < start) {
        LOG.error("Time regressed from {} to {}.", start, now);
        return false;
      }
    } while (!readyResp.isReady() && now < start + timeoutMs);

    // Here if the leader is ready or if there was a timeout.
    if (!readyResp.isReady()) {
      LOG.error("Timed out waiting for leader to be ready for change config.");
      return false;
    } else {
      return true;
    }
  }

  @Override
  public void run() {
    try {
      // Get the master addresses.
      Universe universe = Universe.get(taskParams().universeUUID);
      String masterAddresses = universe.getMasterAddresses();
      LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
               taskParams().universeUUID, masterAddresses);
      if (masterAddresses == null || masterAddresses.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a change config op in " +
            taskParams().universeUUID);
      }

      YBClient client = ybService.getClient(masterAddresses);
      // If a new leader got elected, then the Raft Consensus algorithm requirement does not
      // let it perform any change config ops in the current term without a commit in the same term.
      // So this check waits for a commit of a NO_OP after leader election.
      if (!waitForLeaderReadyToDoChangeConfig(client, client.getDefaultAdminOperationTimeoutMs())) {
        String msg = "Master leader in " + masterAddresses + " not ready for " +
                     " ChangeConfig op via '" + getName() +
                     "'in universe " + taskParams().universeUUID;
        throw new RuntimeException(msg);
      }

      // Get the node details.
      NodeDetails node = universe.getNode(taskParams().nodeName);
      // Perform the change config operation.
      boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
      ChangeConfigResponse response = client.changeMasterConfig(node.private_ip,
                                                                node.masterRpcPort,
                                                                isAddMasterOp);
      if (response.hasError()) {
        String msg = "In " + getName() + ", ChangeConfig response has error " +
                     response.errorMessage();
        LOG.error(msg);
        throw new Exception(msg);
      }
    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }
}

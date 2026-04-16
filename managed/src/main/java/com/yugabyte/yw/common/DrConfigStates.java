package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonValue;
import io.ebean.annotation.DbEnumValue;

/**
 * These states will be used for the user's information. It is specifically useful when a task fails
 * in the middle and the user has a better indication of how much the task has progressed.
 *
 * <p>These should <b>not</b> be used in the backend to decide actions and are only for user's
 * information.
 */
public class DrConfigStates {
  public enum SourceUniverseState {
    Unconfigured("Unconfigured for DR"),
    ReadyToReplicate("Ready to replicate"),
    WaitingForDr("Waiting for DR"),
    ReplicatingData("Replicating data"),
    PreparingSwitchover("Preparing for switchover"),
    SwitchingToDrReplica("Switching to DR replica"),
    DrFailed("Universe marked as DR failed");

    private final String state;

    SourceUniverseState(String state) {
      this.state = state;
    }

    @Override
    @DbEnumValue
    @JsonValue
    public String toString() {
      return this.state;
    }
  }

  public enum TargetUniverseState {
    Unconfigured("Unconfigured for DR"),
    Bootstrapping("Bootstrapping"),
    ReceivingData("Receiving data, Ready for reads"),
    SwitchingToDrPrimary("Switching to DR primary"),
    DrFailed("Universe marked as DR failed");

    private final String state;

    TargetUniverseState(String state) {
      this.state = state;
    }

    @Override
    @DbEnumValue
    @JsonValue
    public String toString() {
      return this.state;
    }
  }

  public enum State {
    Initializing("Initializing"),
    Replicating("Replicating"),
    SwitchoverInProgress("Switchover in Progress"),
    FailoverInProgress("Failover in Progress"),
    Halted("Halted"),
    Updating("Updating"),
    Failed("Failed");

    private final String state;

    State(String state) {
      this.state = state;
    }

    @Override
    @DbEnumValue
    @JsonValue
    public String toString() {
      return this.state;
    }
  }

  public enum InternalState {
    Initialized,
    CreatedNewXClusterConfig,
  }
}

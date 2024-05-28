// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonValue;
import io.ebean.annotation.DbEnumValue;

// Any changes to HaConfigStates must also be reflected in:
// - ui/src/components/ha/dtos.ts
// - ui/src/translations/en.json

public class HaConfigStates {
  public enum GlobalState {
    Unknown("Unknown"),
    NoReplicas("No Replicas"),
    AwaitingReplicas("Awaiting Connection to Replicas"),
    Operational("Operational"),
    Error("Error"),
    Warning("Warning"),
    StandbyConnected("Standby Connected"),
    StandbyDisconnected("Standby Disconnected");

    private final String state;

    GlobalState(String state) {
      this.state = state;
    }

    @Override
    @DbEnumValue
    @JsonValue
    public String toString() {
      return this.name();
    }

    public String getState() {
      return this.state;
    }
  }

  public enum InstanceState {
    AwaitingReplicas("Awaiting Connection to Replicas"),
    Connected("Connected"),
    Disconnected("Disconnected");

    private final String state;

    InstanceState(String state) {
      this.state = state;
    }

    @Override
    @DbEnumValue
    @JsonValue
    public String toString() {
      return this.state;
    }

    public static InstanceState fromString(String input) {
      for (InstanceState state : InstanceState.values()) {
        if (state.state.equalsIgnoreCase(input)) {
          return state;
        }
      }
      throw new IllegalArgumentException("No enum with state: " + input);
    }
  }
}

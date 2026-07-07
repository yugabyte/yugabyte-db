// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

/** Captures a before-to-target state transition for an in-flight universe task. */
@Data
@NoArgsConstructor
@AllArgsConstructor
public class StateTransitionDetails {
  private boolean rollbackSafe;
  private JsonNode delta;
}

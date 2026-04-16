package com.yugabyte.yw.common;

import java.util.HashMap;
import java.util.Map;
import lombok.Data;

@Data
public class UniverseInterruptionResult {
  private String universeName;
  private Map<String, InterruptionStatus> nodesInterruptionStatus;

  public UniverseInterruptionResult(String universeName) {
    this.universeName = universeName;
    this.nodesInterruptionStatus = new HashMap<>();
  }

  // Enum for InterruptionStatus
  public enum InterruptionStatus {
    Interrupted,
    NotInterrupted
  }

  public void addNodeStatus(String nodeName, InterruptionStatus status) {
    this.nodesInterruptionStatus.put(nodeName, status);
  }
}

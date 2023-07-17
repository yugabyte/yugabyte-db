package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.CloudUtil.Protocol;
import java.util.List;
import lombok.Data;

@Data
public class NLBHealthCheckConfiguration {
  private List<Integer> healthCheckPorts;
  private Protocol healthCheckProtocol;
  private String healthCheckPath;

  public NLBHealthCheckConfiguration(
      List<Integer> healthCheckPorts, Protocol healthCheckProtocol, String healthCheckPath) {
    this.healthCheckPorts = healthCheckPorts;
    this.healthCheckProtocol = healthCheckProtocol;
    this.healthCheckPath = healthCheckPath;
  }
}

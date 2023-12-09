package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import lombok.Data;

@Data
public class NLBHealthCheckConfiguration {
  private List<Integer> healthCheckPorts;
  private Protocol healthCheckProtocol;
  private List<String> healthCheckPaths;

  public NLBHealthCheckConfiguration(
      List<Integer> healthCheckPorts, Protocol healthCheckProtocol, List<String> healthCheckPaths) {
    this.healthCheckPorts = healthCheckPorts;
    this.healthCheckProtocol = healthCheckProtocol;
    this.healthCheckPaths = healthCheckPaths;
  }

  public Map<Integer, String> getHealthCheckPortsToPathsMap() {
    if (!healthCheckProtocol.equals(Protocol.HTTP)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Can get a mapping from port to paths only for HTTP health checks. Got: "
              + healthCheckProtocol);
    }
    if (healthCheckPorts.size() != healthCheckPaths.size()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Health check ports and paths must be of the same size. Ports: "
              + healthCheckPorts
              + ", paths: "
              + healthCheckPaths);
    }
    return IntStream.range(0, healthCheckPorts.size()).boxed()
        .collect(Collectors.toMap(healthCheckPorts::get, healthCheckPaths::get)).entrySet().stream()
        .filter(entry -> (entry.getKey() > 0 && entry.getKey() <= 65535))
        .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));
  }
}

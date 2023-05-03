package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;
import org.yb.perf_advisor.configs.UniverseNodeConfigInterface;

@Value
@Jacksonized
@Builder
@JsonIgnoreProperties(ignoreUnknown = true)
@AllArgsConstructor
public class PlatformUniverseNodeConfig implements UniverseNodeConfigInterface {

  @JsonProperty NodeDetails nodeDetails;

  @JsonProperty Universe universe;

  @JsonProperty ShellProcessContext shellProcessContext;

  @Override
  public String getNodePrivateIp() {
    return nodeDetails.cloudInfo.private_ip;
  }

  public String getK8sNamespace() {
    if (CloudType.kubernetes.toString().equals(nodeDetails.cloudInfo.cloud)) {
      return nodeDetails.getK8sNamespace();
    }
    return null;
  }
}

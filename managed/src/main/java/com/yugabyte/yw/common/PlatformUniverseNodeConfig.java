package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.yb.perf_advisor.configs.UniverseNodeConfigInterface;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;

@Value
@Jacksonized
@Builder
@JsonIgnoreProperties(ignoreUnknown = true)
@AllArgsConstructor
public class PlatformUniverseNodeConfig implements UniverseNodeConfigInterface {

  @JsonProperty NodeDetails nodeDetails;

  @JsonProperty Universe universe;

  @JsonProperty ShellProcessContext shellProcessContext;
}

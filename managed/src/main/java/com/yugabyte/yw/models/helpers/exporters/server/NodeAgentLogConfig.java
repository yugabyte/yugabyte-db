package com.yugabyte.yw.models.helpers.exporters.server;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

/**
 * node-agent log export configuration. Internal-only: exposed solely through the unified
 * export-telemetry-configs API.
 */
@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Node Agent Log Configuration")
public class NodeAgentLogConfig extends SimpleServerLogConfig {}

package com.yugabyte.yw.models.helpers.exporters.server;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

/**
 * YB-Controller log export configuration. Internal-only: exposed solely through the unified
 * export-telemetry-configs API.
 */
@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "YB-Controller Log Configuration")
public class ControllerLogConfig extends SimpleServerLogConfig {}

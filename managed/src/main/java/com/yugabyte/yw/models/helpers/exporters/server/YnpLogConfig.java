package com.yugabyte.yw.models.helpers.exporters.server;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

/**
 * YNP (Yugabyte Node Provisioning) log export configuration. Internal-only: exposed solely through
 * the unified export-telemetry-configs API.
 */
@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "YNP Log Configuration")
public class YnpLogConfig extends SimpleServerLogConfig {}

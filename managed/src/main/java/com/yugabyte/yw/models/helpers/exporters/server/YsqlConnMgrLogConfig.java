package com.yugabyte.yw.models.helpers.exporters.server;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

/**
 * YSQL Connection Manager log export configuration. Internal-only: exposed solely through the
 * unified export-telemetry-configs API.
 */
@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "YSQL Connection Manager Log Configuration")
public class YsqlConnMgrLogConfig extends SimpleServerLogConfig {}

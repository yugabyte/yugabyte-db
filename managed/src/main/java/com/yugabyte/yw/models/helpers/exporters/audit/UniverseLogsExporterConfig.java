package com.yugabyte.yw.models.helpers.exporters.audit;

import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Data
@EqualsAndHashCode(callSuper = true)
@ToString(callSuper = true)
@ApiModel(description = "Universe Logs Exporter Config")
public class UniverseLogsExporterConfig extends UniverseExporterConfig {}

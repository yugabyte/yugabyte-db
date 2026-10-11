package com.yugabyte.yw.models.helpers.exporters.query;

import com.yugabyte.yw.models.helpers.exporters.BatchedLogsExporterConfig;
import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Data
@EqualsAndHashCode(callSuper = true)
@ToString(callSuper = true)
@ApiModel(description = "Universe Logs Exporter Config")
public class UniverseQueryLogsExporterConfig extends BatchedLogsExporterConfig {}

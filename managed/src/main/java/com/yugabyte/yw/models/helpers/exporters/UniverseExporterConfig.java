package com.yugabyte.yw.models.helpers.exporters;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
@ApiModel(description = "Universe Exporter Config")
public class UniverseExporterConfig {

  @NotNull
  @ApiModelProperty(value = "Exporter uuid", accessMode = READ_ONLY)
  private UUID exporterUuid;

  @NotNull
  @ApiModelProperty(value = "Additional tags", accessMode = READ_WRITE)
  private Map<String, String> additionalTags = new HashMap<>();

  // A request without tags must equal the saved row, which reads back as {}, or
  // TelemetryConfig.diff sees a change and the export task restarts the DB processes.
  public void setAdditionalTags(Map<String, String> additionalTags) {
    this.additionalTags = additionalTags != null ? additionalTags : new HashMap<>();
  }
}

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
}

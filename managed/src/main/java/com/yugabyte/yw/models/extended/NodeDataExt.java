package com.yugabyte.yw.models.extended;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.HealthCheck.Details.NodeData;
import com.yugabyte.yw.models.common.YBADeprecated;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(description = "Health check results for node")
public class NodeDataExt {

  @JsonUnwrapped private NodeData nodeData;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Deprecated: Use timestampIso instead")
  @YBADeprecated(sinceDate = "2023-02-17", sinceYBAVersion = "2.17.2.0")
  private Date timestamp;
}

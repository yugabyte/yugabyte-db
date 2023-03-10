package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.AlertChannelTemplates;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel("Alert channel templates ext with default values")
public class AlertChannelTemplatesExt {
  @JsonUnwrapped AlertChannelTemplates channelTemplates;

  @ApiModelProperty("Default title template")
  String defaultTitleTemplate;

  @ApiModelProperty("Default text template")
  String defaultTextTemplate;
}

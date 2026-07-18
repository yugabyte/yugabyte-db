package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.AlertChannelTemplates;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel("Alert channel templates for notification preview")
public class AlertChannelTemplatesPreview {
  @JsonUnwrapped AlertChannelTemplates channelTemplates;

  @ApiModelProperty("Title template with HTML highlighting")
  String highlightedTitleTemplate;

  @ApiModelProperty("Text template with HTML highlighting")
  String highlightedTextTemplate;
}

package com.yugabyte.yw.controllers.apiModels;

import io.swagger.annotations.ApiModel;
import java.util.UUID;
import play.data.validation.Constraints;

@ApiModel(description = "url to release TGZ to extract metadata from")
public class ExtractMetadata {
  public UUID uuid;
  @Constraints.Required public String url;
}

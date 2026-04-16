package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Data;

@Data
@AllArgsConstructor
public class SupportBundleSizeEstimateResponse {

  @ApiModelProperty(
      value =
          "A map of universe node names to component sizes for the given support bundle payload."
              + " Global component sizes are mapped to \"YBA\" node.")
  private Map<String, Map<String, Long>> data;
}

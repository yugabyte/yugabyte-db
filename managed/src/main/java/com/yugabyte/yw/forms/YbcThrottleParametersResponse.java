package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;
import lombok.Setter;

@ApiModel(description = "YB-Controller throttle parameters response")
@Data
public class YbcThrottleParametersResponse {

  @ApiModelProperty(value = "Map of YBC throttle parameters")
  private Map<String, ThrottleParamValue> throttleParamsMap;

  @AllArgsConstructor
  @Getter
  @Setter
  public static class ThrottleParamValue {
    int currentValue;
    PresetThrottleValues presetValues;
  }

  @AllArgsConstructor
  @Getter
  @Setter
  public static class PresetThrottleValues {
    int defaultValue;
    int minValue;
    int maxValue;
  }
}

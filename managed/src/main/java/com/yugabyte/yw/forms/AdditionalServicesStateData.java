// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.function.Consumer;
import lombok.Data;
import org.apache.commons.lang3.StringUtils;

@ApiModel(description = "Additional services state")
@Data
public class AdditionalServicesStateData {
  @Data
  @ApiModel(description = "State for earlyoom service")
  public static class EarlyoomConfig {
    @ApiModelProperty private boolean enabled;

    @ApiModelProperty private int availMemoryTermPercent;

    @ApiModelProperty private int availMemoryKillPercent;

    @ApiModelProperty private int availMemoryTermKb;

    @ApiModelProperty private int availMemoryKillKb;

    @ApiModelProperty private int reportInterval;

    @ApiModelProperty private String preferPattern;
  }

  @ApiModelProperty("Configuration for earlyoom service.")
  private EarlyoomConfig earlyoomConfig;

  /**
   * Parse earlyoom config from command line args used for configuration.
   *
   * @param enabled Whether earlyoom enabled or not now.
   * @param args Command line args used for earlyoom configuration.
   * @param ignoreErrors Whether to ignore errors during conversion (use default values instead)
   * @return
   */
  public static EarlyoomConfig fromArgs(boolean enabled, String args, boolean ignoreErrors) {
    EarlyoomConfig result = new EarlyoomConfig();
    result.setEnabled(enabled);
    String[] split = args.split(" ");
    for (int i = 0; i < split.length; i++) {
      String key = split[i];
      if (key.startsWith("-")) {
        String value = "";
        if (i < split.length - 1 && !split[i + 1].startsWith("-")) {
          value = split[++i];
        }
        if (key.equals("--prefer")) {
          result.setPreferPattern(value);
        } else if (key.equals("-M")) {
          String[] spl = value.split(",");
          parseInt("available memory term kb", spl[0], result::setAvailMemoryTermKb, ignoreErrors);
          parseInt("available memory kill kb", spl[1], result::setAvailMemoryKillKb, ignoreErrors);
        } else if (key.equals("-m")) {
          String[] spl = value.split(",");
          parseInt(
              "available memory term percent",
              spl[0],
              result::setAvailMemoryTermPercent,
              ignoreErrors);
          parseInt(
              "available memory kill percent",
              spl[1],
              result::setAvailMemoryKillPercent,
              ignoreErrors);
        } else if (key.equals("-r")) {
          parseInt("report interval", value, result::setReportInterval, ignoreErrors);
        }
      }
    }
    return result;
  }

  public static String toArgs(EarlyoomConfig config) {
    StringBuilder sb = new StringBuilder();
    appendIfPresent(sb, " -M", config.getAvailMemoryTermKb(), config.getAvailMemoryKillKb());
    appendIfPresent(
        sb, " -m", config.getAvailMemoryTermPercent(), config.getAvailMemoryKillPercent());
    if (StringUtils.isNotEmpty(config.getPreferPattern())) {
      sb.append(" --prefer '").append(config.getPreferPattern()).append("'");
    }
    if (config.getReportInterval() > 0) {
      sb.append(" -r ").append(config.getReportInterval());
    }
    return sb.toString().trim();
  }

  private static void appendIfPresent(StringBuilder sb, String key, int value0, int value1) {
    if (value0 > 0 || value1 > 0) {
      sb.append(key).append(" ").append(value0 == 0 ? "" : String.valueOf(value0));
      if (value1 > 0) {
        sb.append(",").append(value1);
      }
    }
  }

  private static void parseInt(
      String key, String val, Consumer<Integer> setter, boolean ignoreErrors) {
    try {
      setter.accept(Integer.parseInt(val));
    } catch (Exception e) {
      if (!ignoreErrors) {
        throw new IllegalArgumentException("Failed to convert " + key + ": value = " + val);
      }
    }
  }
}

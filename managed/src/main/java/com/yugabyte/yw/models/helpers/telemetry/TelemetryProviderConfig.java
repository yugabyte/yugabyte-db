package com.yugabyte.yw.models.helpers.telemetry;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonSubTypes.Type;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.yugabyte.yw.common.ApiHelper;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
import lombok.Data;
import org.apache.commons.lang3.StringUtils;

@Data
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, property = "type")
@JsonSubTypes({
  @Type(value = DataDogConfig.class, name = "DATA_DOG"),
  @Type(value = SplunkConfig.class, name = "SPLUNK"),
  @Type(value = AWSCloudWatchConfig.class, name = "AWS_CLOUDWATCH"),
  @Type(value = GCPCloudMonitoringConfig.class, name = "GCP_CLOUD_MONITORING"),
  @Type(value = LokiConfig.class, name = "LOKI"),
  @Type(value = DynatraceConfig.class, name = "DYNATRACE")
})
@ApiModel(description = "Telemetry Provider Configuration")
public class TelemetryProviderConfig {
  @ApiModelProperty(value = "Telemetry Provider Type", accessMode = AccessMode.READ_WRITE)
  private ProviderType type;

  public void validate(ApiHelper apiHelper) {
    // To be overridden in child classes if validation is required.
  }

  /**
   * Credential values as configured, so they can be redacted by value wherever they are rendered.
   * Not a bean property, so it is neither serialized nor documented.
   */
  public List<String> secretValues() {
    return Collections.emptyList();
  }

  protected static List<String> nonEmpty(String... values) {
    return Arrays.stream(values).filter(StringUtils::isNotEmpty).collect(Collectors.toList());
  }
}

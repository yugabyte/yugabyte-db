package com.yugabyte.yw.models.helpers.provider;

import java.util.HashMap;
import java.util.Map;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;

import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiParam;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class AWSCloudInfo implements CloudInfoInterface {

  private static final Map<String, String> configKeyMap =
      ImmutableMap.of(
          "awsAccessKeyID",
          "AWS_ACCESS_KEY_ID",
          "awsAccessKeySecret",
          "AWS_SECRET_ACCESS_KEY",
          "awsHostedZoneId",
          "HOSTED_ZONE_ID",
          "awsHostedZoneName",
          "HOSTED_ZONE_NAME");

  @JsonAlias("AWS_ACCESS_KEY_ID")
  @ApiModelProperty
  public String awsAccessKeyID;

  @JsonAlias("AWS_SECRET_ACCESS_KEY")
  @ApiModelProperty
  public String awsAccessKeySecret;

  @JsonAlias("HOSTED_ZONE_ID")
  @ApiModelProperty
  @ApiParam(value = "Route 53 Zone ID")
  public String awsHostedZoneId;

  @JsonAlias("HOSTED_ZONE_NAME")
  @ApiModelProperty
  public String awsHostedZoneName;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (awsAccessKeyID != null) {
      envVars.put("AWS_ACCESS_KEY_ID", awsAccessKeyID);
      envVars.put("AWS_SECRET_ACCESS_KEY", awsAccessKeySecret);
    }
    if (awsHostedZoneId != null) {
      envVars.put("HOSTED_ZONE_ID", awsHostedZoneId);
    }
    if (awsHostedZoneName != null) {
      envVars.put("HOSTED_ZONE_NAME", awsHostedZoneName);
    }

    return envVars;
  }

  @JsonIgnore
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    for (Map.Entry<String, String> entry : configKeyMap.entrySet()) {
      if (config.get(entry.getKey()) != null) {
        config.put(entry.getValue(), config.get(entry.getKey()));
        config.remove(entry.getKey());
      }
    }
    return config;
  }

  @JsonIgnore
  public void withSensitiveDataMasked() {
    this.awsAccessKeyID = CommonUtils.getMaskedValue(awsAccessKeyID);
    this.awsAccessKeySecret = CommonUtils.getMaskedValue(awsAccessKeySecret);
  }
}

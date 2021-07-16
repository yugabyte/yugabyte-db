// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;

/** This class will be used by Customer details API. */
@ApiModel(value = "Customer Detail", description = "Customers features and Universe UUID.")
public class CustomerDetailsData {

  @ApiModelProperty(value = "User uuid", accessMode = READ_ONLY)
  public String uuid;

  @ApiModelProperty(value = "Customer code", example = "admin", required = true)
  public String code;

  @ApiModelProperty(value = "Name of customer", example = "sridhar", required = true)
  public String name;

  @ApiModelProperty(
      value = "Creation time",
      example = "2021-06-17 15:00:05",
      accessMode = READ_ONLY)
  public Date creationDate;

  @ApiModelProperty(value = "Features", accessMode = READ_ONLY)
  public JsonNode features;

  @ApiModelProperty(
      value = "Associated Universe Id's",
      accessMode = READ_ONLY,
      example =
          "[\"c3595ca7-68a3-47f0-b1b2-1725886d5ed5\", \"9e0bb733-556c-4935-83dd-6b742a2c32e6\"]")
  public List<UUID> universeUUIDs;

  @ApiModelProperty(value = "Customer id", accessMode = READ_ONLY)
  public int customerId;

  @ApiModelProperty(value = "Alerts", accessMode = READ_ONLY)
  public AlertingData alertingData;

  @ApiModelProperty(value = "SMTP", accessMode = READ_ONLY)
  public SmtpData smtpData;

  @ApiModelProperty(value = "Call home level", accessMode = READ_ONLY, example = "MEDIUM")
  public String callhomeLevel;
}

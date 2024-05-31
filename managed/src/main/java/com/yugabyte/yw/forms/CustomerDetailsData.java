// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.alerts.SmtpData;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;

/** This class will be used by Customer details API. */
@ApiModel(
    description =
        "Customer details, including their universe UUIDs. Only the customer code and name are"
            + " modifiable.")
public class CustomerDetailsData {

  @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
  public String uuid;

  @ApiModelProperty(value = "Customer code", example = "admin", required = true)
  public String code;

  @ApiModelProperty(value = "Customer name", example = "Sridhar", required = true)
  public String name;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation timestamp",
      example = "2022-12-12T13:07:18Z",
      accessMode = READ_ONLY)
  public Date creationDate;

  @ApiModelProperty(value = "UI_ONLY", hidden = true, accessMode = READ_ONLY)
  public JsonNode features;

  @ApiModelProperty(
      value = "Associated universe IDs",
      accessMode = READ_ONLY,
      example =
          "[\"c3595ca7-68a3-47f0-b1b2-1725886d5ed5\", \"9e0bb733-556c-4935-83dd-6b742a2c32e6\"]")
  public List<UUID> universeUUIDs;

  @ApiModelProperty(value = "Customer ID", accessMode = READ_ONLY)
  public int customerId;

  @ApiModelProperty(value = "Alerts", accessMode = READ_ONLY)
  public AlertingData alertingData;

  @ApiModelProperty(value = "SMTP", accessMode = READ_ONLY)
  public SmtpData smtpData;

  @ApiModelProperty(value = "Call-home level", accessMode = READ_ONLY, example = "MEDIUM")
  public String callhomeLevel;
}

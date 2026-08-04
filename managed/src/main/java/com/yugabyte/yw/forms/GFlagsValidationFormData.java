package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;

/** This class will be used by the API for gflags validation. */
public class GFlagsValidationFormData {

  @JsonProperty(value = "gflags")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. List of gflags to validate")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public List<GFlagsValidationRequest> gflagsList;

  /** Structure to capture gflags validation request */
  public static class GFlagsValidationRequest {
    @JsonProperty(value = "Name")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    @ApiModelProperty(value = "WARNING: This is a preview API that could change. Name of the gflag")
    public String name;

    @JsonProperty(value = "MASTER")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Value of the gflag for master")
    public String masterValue;

    @JsonProperty(value = "TSERVER")
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Value of the gflag for tserver")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    public String tserverValue;
  }

  /** Structure to send gflags validation errors response */
  public static class GFlagsValidationResponse {

    @JsonProperty(value = "Name")
    @ApiModelProperty(value = "WARNING: This is a preview API that could change. Name of the gflag")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    public String name;

    @JsonProperty(value = "MASTER")
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Validation details for master")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public GFlagValidationDetails masterResponse;

    @JsonProperty(value = "TSERVER")
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Validation details for tserver")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public GFlagValidationDetails tserverResponse;
  }

  /** Structure to store gflag validation details */
  public static class GFlagValidationDetails {

    @JsonProperty(value = "exist")
    @ApiModelProperty(
        value =
            "WARNING: This is a preview API that could change. Flag to indicate if gflag exists")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    public boolean exist = false;

    @JsonProperty(value = "error")
    @ApiModelProperty(
        value =
            "WARNING: This is a preview API that could change. Error message if gflag is invalid")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
    public String error = null;
  }
}

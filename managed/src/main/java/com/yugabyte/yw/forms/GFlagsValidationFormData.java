package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;

/** This class will be used by the API and UI Form Elements to for gflags valdiation. */
public class GFlagsValidationFormData {

  @JsonProperty(value = "gflags")
  public List<GFlagsValidationRequest> gflagsList;

  /** Structure to capture gflags validation request */
  public static class GFlagsValidationRequest {
    @JsonProperty(value = "Name", required = true)
    public String name;

    @JsonProperty(value = "MASTER")
    public String masterValue;

    @JsonProperty(value = "TSERVER")
    public String tserverValue;
  }

  /** Structure to send gflags validation errors reponse */
  public static class GFlagsValidationResponse {

    @JsonProperty(value = "Name")
    public String name;

    @JsonProperty(value = "MASTER")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public GFlagValidationDetails masterResponse;

    @JsonProperty(value = "TSERVER")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public GFlagValidationDetails tserverResponse;
  }

  /** Structure to store gflag validation details */
  public static class GFlagValidationDetails {

    @JsonProperty(value = "exist")
    public boolean exist = false;

    @JsonProperty(value = "error")
    public String error = null;
  }
}

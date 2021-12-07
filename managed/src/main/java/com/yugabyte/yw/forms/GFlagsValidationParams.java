package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

/** This class will be used by the API and UI Form Elements to for gflags valdiation. */
public class GFlagsValidationParams {

  /** Structure to capture gflags validation request */
  public static class GFlagsValidationRequest {
    @JsonProperty(value = "MASTER")
    public Map<String, String> masterGFlags;

    @JsonProperty(value = "TSERVER")
    public Map<String, String> tserverGFlags;
  }

  /** Structure to send gflags validation errors reponse */
  public static class GFlagsValidationResponse {
    @JsonProperty(value = "MASTER")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public Map<String, Map<String, String>> masterGFlags;

    @JsonProperty(value = "TSERVER")
    @JsonInclude(value = JsonInclude.Include.ALWAYS)
    public Map<String, Map<String, String>> tserverGFlags;
  }
}

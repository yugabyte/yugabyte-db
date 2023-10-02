// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import lombok.AllArgsConstructor;

@AllArgsConstructor
public class KubernetesOverridesResponse {
  @JsonProperty(value = "overridesErrors")
  @JsonInclude(value = JsonInclude.Include.ALWAYS)
  public List<KubernetesOverrideError> overridesErrors;

  public KubernetesOverridesResponse() {
    overridesErrors = new ArrayList<>();
  }

  public static KubernetesOverridesResponse convertErrorsToKubernetesOverridesResponse(
      Set<String> overrideErrors) {
    KubernetesOverridesResponse result = new KubernetesOverridesResponse();
    for (String error : overrideErrors) {
      result.overridesErrors.add(new KubernetesOverrideError(error));
    }

    return result;
  }
}

class KubernetesOverrideError {
  @JsonProperty(value = "errorString")
  public String errorString;

  // We can add line number in yaml or possible fix or any other properties later on.

  public KubernetesOverrideError(String errorString) {
    this.errorString = errorString.trim();
  }
}

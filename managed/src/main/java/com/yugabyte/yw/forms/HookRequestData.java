// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.UNAUTHORIZED;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Hook;
import java.util.Map;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@Getter
@Setter
public class HookRequestData {
  @Constraints.Required()
  @Constraints.MaxLength(100)
  private String name;

  @Constraints.Required() private Hook.ExecutionLang executionLang;

  private boolean useSudo = false;

  private Map<String, String> runtimeArgs;

  @JsonProperty(access = JsonProperty.Access.READ_ONLY)
  private String hookText;

  public void verify(UUID customerUUID, boolean isNameChanged, boolean isSudoEnabled) {
    if (isNameChanged && Hook.getByName(customerUUID, name) != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Hook with this name already exists: " + name);
    }
    if (useSudo && !isSudoEnabled) {
      throw new PlatformServiceException(
          UNAUTHORIZED,
          "Creating custom hooks with superuser privileges is not enabled on this Anywhere"
              + " instance");
    }
  }
}

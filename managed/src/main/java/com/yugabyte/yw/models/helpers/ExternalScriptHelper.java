package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonInclude;
import javax.inject.Singleton;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Singleton
public class ExternalScriptHelper {

  public static final String EXT_SCRIPT_RUNTIME_CONFIG_PATH = "yb.external_script";
  public static final String EXT_SCRIPT_CONTENT_CONF_PATH = "yb.external_script.content";
  public static final String EXT_SCRIPT_PARAMS_CONF_PATH = "yb.external_script.params";
  public static final String EXT_SCRIPT_SCHEDULE_CONF_PATH = "yb.external_script.schedule";
  public static final String EXT_SCRIPT_ACCESS_FULL_PATH = "yb.security.enable_external_script";

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class ExternalScriptConfObject {

    String content;

    String params;

    String schedule;
  }
}

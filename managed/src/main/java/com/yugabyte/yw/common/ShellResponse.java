// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import java.util.List;
import lombok.Data;
import org.apache.commons.lang3.StringUtils;

@Data
public class ShellResponse {
  // Some known error codes for shell process.
  public static final int ERROR_CODE_SUCCESS = 0;
  public static final int ERROR_CODE_GENERIC_ERROR = -1;
  public static final int ERROR_CODE_EXECUTION_CANCELLED = -2;

  public int code = ERROR_CODE_SUCCESS;
  public String message = null;
  public long durationMs = 0;
  public String description = null;

  public static ShellResponse create(int code, String message) {
    ShellResponse sr = new ShellResponse();
    sr.code = code;
    sr.message = message;
    return sr;
  }

  public void setDescription(List<String> command) {
    description = StringUtils.abbreviateMiddle(String.join(" ", command), " ... ", 140);
  }

  public boolean isSuccess() {
    return code == ERROR_CODE_SUCCESS;
  }
}

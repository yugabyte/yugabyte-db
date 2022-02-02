package com.yugabyte.yw.common;

import java.util.List;
import org.apache.commons.lang3.StringUtils;

public class ShellResponse {
  public static final int ERROR_CODE_SUCCESS = 0;

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
}

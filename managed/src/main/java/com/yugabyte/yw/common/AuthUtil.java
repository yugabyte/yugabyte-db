package com.yugabyte.yw.common;

import java.util.Base64;
import java.util.Collections;
import java.util.Map;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.HttpHeaders;

public class AuthUtil {

  public static Map<String, String> getBasicAuthHeader(String username, String password) {
    if (StringUtils.isBlank(username) && StringUtils.isBlank(password)) {
      return Collections.emptyMap();
    }
    String encoding = Base64.getEncoder().encodeToString((username + ":" + password).getBytes());
    return Collections.singletonMap(HttpHeaders.AUTHORIZATION, "Basic " + encoding);
  }
}

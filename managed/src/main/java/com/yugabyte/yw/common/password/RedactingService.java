// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.password;

import com.fasterxml.jackson.databind.JsonNode;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.yugabyte.yw.common.audit.AuditService;
import javax.inject.Singleton;

@Singleton
public class RedactingService {

  public static JsonNode filterSecretFields(JsonNode input) {
    if (input == null) {
      return null;
    }

    DocumentContext context = JsonPath.parse(input.deepCopy(), AuditService.JSONPATH_CONFIG);
    AuditService.SECRET_JSON_PATHS.forEach(
        path -> {
          JsonNode contents = context.read(path);
          if (contents.isArray()) {
            for (JsonNode content : contents) {
              if (content.asText().contains("*")) {
                // If already masked, we will avoid redacting the string, as for some
                // values(masked by model itself) client expects them to be masked not redacted.
                // Case: Return in API response unmasked.
                continue;
              } else {
                context.set(path, AuditService.SECRET_REPLACEMENT);
              }
            }
          }
        });

    return context.json();
  }

  public static String redactString(String input) {
    String length = ((Integer) input.length()).toString();
    String regex = "(.)" + "{" + length + "}";
    String output = input.replaceAll(regex, AuditService.SECRET_REPLACEMENT);
    return output;
  }
}

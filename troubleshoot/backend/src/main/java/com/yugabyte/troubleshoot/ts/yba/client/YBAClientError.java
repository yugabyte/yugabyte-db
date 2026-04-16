package com.yugabyte.troubleshoot.ts.yba.client;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.Getter;
import org.springframework.http.HttpStatusCode;

@Getter
public class YBAClientError extends RuntimeException {
  private final HttpStatusCode statusCode;
  private final JsonNode error;

  public YBAClientError(HttpStatusCode statusCode) {
    this(statusCode, null);
  }

  public YBAClientError(HttpStatusCode statusCode, JsonNode error) {
    this.statusCode = statusCode;
    this.error = error;
  }
}

package com.yugabyte.troubleshoot.ts.metric.client;

import lombok.Getter;
import org.springframework.http.HttpStatusCode;

@Getter
public class PrometheusError extends RuntimeException {
  private final HttpStatusCode statusCode;
  private final String errorType;
  private final String error;

  public PrometheusError(HttpStatusCode statusCode) {
    this(statusCode, null, null);
  }

  public PrometheusError(HttpStatusCode statusCode, String errorType, String error) {
    this.statusCode = statusCode;
    this.errorType = errorType;
    this.error = error;
  }
}

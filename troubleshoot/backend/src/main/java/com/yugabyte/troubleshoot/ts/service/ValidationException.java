package com.yugabyte.troubleshoot.ts.service;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.Getter;
import lombok.experimental.Accessors;

@Getter
public class ValidationException extends RuntimeException {
  private final List<ValidationErrors> validationErrors;

  public ValidationException(List<ValidationErrors> validationErrors) {
    super(
        "Validation failed: {"
            + validationErrors.stream()
                .map(
                    e ->
                        "\""
                            + e.getPropertyPath()
                            + "\":"
                            + "["
                            + e.getErrors().stream()
                                .map(message -> "\"" + message + "\"")
                                .collect(Collectors.joining(","))
                            + "]}")
                .collect(Collectors.joining(",")));
    this.validationErrors = validationErrors;
  }

  @Data
  @Accessors(chain = true)
  public static class ValidationErrors {
    private String propertyPath;
    private List<String> errors;

    public ValidationErrors(String propertyPath) {
      this.propertyPath = propertyPath;
      this.errors = new ArrayList<>();
    }
  }
}

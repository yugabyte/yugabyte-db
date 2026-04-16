package com.yugabyte.troubleshoot.ts.controllers;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.service.ValidationException;
import org.springframework.http.*;
import org.springframework.web.bind.annotation.ControllerAdvice;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.servlet.mvc.method.annotation.ResponseEntityExceptionHandler;

@ControllerAdvice
public class TSExceptionHandler extends ResponseEntityExceptionHandler {

  @ExceptionHandler(ValidationException.class)
  public final ResponseEntity<Object> handle(Exception ex) {
    ValidationException validationException = (ValidationException) ex;
    ProblemDetail problemDetail =
        ProblemDetail.forStatusAndDetail(HttpStatus.BAD_REQUEST, "Validation error");
    problemDetail.setTitle("Bad Request");
    problemDetail.setProperties(
        ImmutableMap.of("errors", validationException.getValidationErrors()));
    return new ResponseEntity<>(problemDetail, HttpStatus.BAD_REQUEST);
  }
}

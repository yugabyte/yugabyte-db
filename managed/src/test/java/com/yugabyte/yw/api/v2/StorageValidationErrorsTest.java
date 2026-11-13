// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import api.v2.handlers.StorageValidationErrors;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import play.libs.Json;
import software.amazon.awssdk.awscore.exception.AwsErrorDetails;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.s3.model.S3Exception;

/** How a failed storage check is classified; the ValidateStorageConfig task reports the code. */
public class StorageValidationErrorsTest {

  /** The v2 error of a storage check that fails with the given exception. */
  private static JsonNode checkFailingWith(Throwable failure, int expectedStatus) {
    PlatformServiceException e = StorageValidationErrors.toV2Error(failure);
    assertEquals(e.getContentJson().toString(), expectedStatus, e.getHttpStatus());
    return e.getContentJson().get("error");
  }

  /** A v1 validator error for BACKUP_LOCATION, as the storage validators throw it. */
  private static PlatformServiceException v1FieldError(int status, Throwable cause) {
    PlatformServiceException e =
        new PlatformServiceException(
            status, Json.toJson(Map.of("data.BACKUP_LOCATION", List.of("check failed"))));
    if (cause != null) {
      e.initCause(cause);
    }
    return e;
  }

  @Test
  public void providerErrors() {
    // The provider's 403 is the client's storage problem.
    S3Exception accessDenied =
        (S3Exception)
            S3Exception.builder()
                .statusCode(403)
                .awsErrorDetails(AwsErrorDetails.builder().errorCode("AccessDenied").build())
                .message("Access Denied")
                .build();
    JsonNode error = checkFailingWith(v1FieldError(400, accessDenied), 422);
    assertEquals(StorageValidationErrors.VALIDATION_FAILED, error.get("code").asText());
    assertEquals(403, error.get("provider_status").asInt());
    assertEquals("AccessDenied", error.get("provider_error_code").asText());
    assertEquals(
        "check failed", error.get("errors").get("storage.backup_location").get(0).asText());

    // Throttling or a provider failure: worth retrying.
    S3Exception slowDown =
        (S3Exception)
            S3Exception.builder()
                .statusCode(503)
                .awsErrorDetails(AwsErrorDetails.builder().errorCode("SlowDown").build())
                .message("Slow Down")
                .build();
    error = checkFailingWith(v1FieldError(400, slowDown), 503);
    assertEquals(StorageValidationErrors.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertEquals(503, error.get("provider_status").asInt());

    // An SDK exception the validator didn't wrap.
    error = checkFailingWith(slowDown, 503);
    assertEquals(StorageValidationErrors.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertTrue(error.get("errors").get("storage").get(0).asText().startsWith("Slow Down"));

    // A timeout on the way to the provider: no provider status.
    SdkClientException timeout =
        SdkClientException.builder()
            .message("Unable to execute HTTP request")
            .cause(new SocketTimeoutException("Read timed out"))
            .build();
    error = checkFailingWith(v1FieldError(400, timeout), 503);
    assertEquals(StorageValidationErrors.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertFalse(error.has("provider_status"));

    // An unknown host is more likely a wrong endpoint.
    SdkClientException unknownHost =
        SdkClientException.builder()
            .message("Unable to execute HTTP request")
            .cause(new UnknownHostException("bucket.example.invalid"))
            .build();
    error = checkFailingWith(v1FieldError(400, unknownHost), 422);
    assertEquals(StorageValidationErrors.VALIDATION_FAILED, error.get("code").asText());

    // A failed read of the test object is a plain message with a stack trace: the provider's
    // message is reported instead.
    PlatformServiceException readFailed =
        new PlatformServiceException(
            417, "Error reading test object x, exception occurred: at a.b.C.d(C.java:1)");
    readFailed.initCause(slowDown);
    error = checkFailingWith(readFailed, 503);
    assertEquals(StorageValidationErrors.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertEquals(503, error.get("provider_status").asInt());
    assertTrue(error.get("errors").get("storage").get(0).asText().startsWith("Slow Down"));
    assertFalse(error.toString().contains("C.java"));

    // Azure's blob read fails with an IOException around the SDK's error.
    PlatformServiceException azureReadFailed =
        new PlatformServiceException(417, "Error reading test blob x");
    azureReadFailed.initCause(
        new java.io.IOException(new SocketTimeoutException("Read timed out")));
    error = checkFailingWith(azureReadFailed, 503);
    assertEquals(StorageValidationErrors.PROVIDER_UNAVAILABLE, error.get("code").asText());

    // The Azure SDK doesn't wrap network errors in an SDK type: the validator wraps its reactor
    // exception for an unknown host, and rethrows any other one as is.
    RuntimeException azureUnknownHost =
        new RuntimeException("reactor", new UnknownHostException("account.example.invalid"));
    error = checkFailingWith(v1FieldError(400, azureUnknownHost), 422);
    assertEquals(StorageValidationErrors.VALIDATION_FAILED, error.get("code").asText());
    checkFailingWith(new RuntimeException(new ConnectException("Connection refused")), 422);
  }

  @Test
  public void validatorErrorsWithoutProvider() {
    // Found without reaching the storage, such as a malformed location.
    JsonNode error = checkFailingWith(v1FieldError(400, null), 400);
    assertEquals(StorageValidationErrors.CONFIG_INVALID, error.get("code").asText());

    // The storage answered, but wrongly, such as a test object read back different.
    error = checkFailingWith(v1FieldError(417, null), 422);
    assertEquals(StorageValidationErrors.VALIDATION_FAILED, error.get("code").asText());

    // Anything else is a YBA bug.
    checkFailingWith(new IllegalStateException("bug"), 500);
  }
}

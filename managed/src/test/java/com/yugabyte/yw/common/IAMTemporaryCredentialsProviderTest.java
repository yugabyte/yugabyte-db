// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.isRetryableFailure;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.net.SocketTimeoutException;
import org.junit.Test;
import software.amazon.awssdk.awscore.exception.AwsErrorDetails;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.sts.model.StsException;

/**
 * Which failures of an IAM credential source leave it to the S3 client's retries, and which mean
 * the host has nothing usable for it.
 */
public class IAMTemporaryCredentialsProviderTest {

  private static StsException stsReply(int statusCode, String errorCode) {
    return (StsException)
        StsException.builder()
            .message(errorCode)
            .statusCode(statusCode)
            .awsErrorDetails(
                AwsErrorDetails.builder().errorCode(errorCode).errorMessage(errorCode).build())
            .build();
  }

  @Test
  public void testThrottlesServerErrorsAndIoErrorsAreRetryable() {
    assertTrue(isRetryableFailure(stsReply(400, "Throttling")));
    assertTrue(isRetryableFailure(stsReply(429, "TooManyRequestsException")));
    assertTrue(isRetryableFailure(stsReply(503, "ServiceUnavailable")));
    assertTrue(
        isRetryableFailure(new RuntimeException("wrapped", stsReply(500, "InternalFailure"))));
    assertTrue(
        isRetryableFailure(
            SdkClientException.builder()
                .message("Unable to execute HTTP request: Read timed out")
                .cause(new SocketTimeoutException("Read timed out"))
                .build()));
  }

  /** AWS rejecting what the host has gets the same answer on every retry. */
  @Test
  public void testRejectionsByAwsAreNotRetryable() {
    assertFalse(isRetryableFailure(stsReply(403, "AccessDenied")));
    assertFalse(isRetryableFailure(stsReply(400, "InvalidIdentityToken")));
    assertFalse(isRetryableFailure(stsReply(400, "ExpiredTokenException")));
    assertFalse(isRetryableFailure(new RuntimeException("wrapped", stsReply(403, "AccessDenied"))));
  }

  @Test
  public void testNothingOnTheHostIsNotRetryable() {
    assertFalse(isRetryableFailure(new RuntimeException("AWS_ROLE_ARN: blank variable value.")));
    assertFalse(
        isRetryableFailure(
            new RuntimeException("Fetching Web Identity token failed: File does not exist")));
    assertFalse(
        isRetryableFailure(
            SdkClientException.builder()
                .message("Profile file contained no credentials for profile 'default'")
                .build()));
    assertFalse(
        isRetryableFailure(
            SdkClientException.builder()
                .message(
                    "Cannot fetch credentials from container - neither"
                        + " AWS_CONTAINER_CREDENTIALS_FULL_URI or"
                        + " AWS_CONTAINER_CREDENTIALS_RELATIVE_URI environment variables are set.")
                .build()));
    assertFalse(
        isRetryableFailure(
            new IllegalStateException(
                "To use Sso related properties in the 'default' profile, the 'sso' service module"
                    + " must be on the class path.")));
  }
}

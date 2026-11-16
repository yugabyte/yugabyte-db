// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.isRemoteFailure;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.net.SocketTimeoutException;
import org.junit.Test;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.sts.model.StsException;

/**
 * Which failures of an IAM credential source leave it to the S3 client's retries, and which mean
 * the host has nothing for it.
 */
public class IAMTemporaryCredentialsProviderTest {

  @Test
  public void testRepliesFromAwsAndIoErrorsAreRemoteFailures() {
    assertTrue(
        isRemoteFailure(StsException.builder().message("Rate exceeded").statusCode(400).build()));
    assertTrue(
        isRemoteFailure(
            SdkClientException.builder()
                .message("Unable to execute HTTP request: Read timed out")
                .cause(new SocketTimeoutException("Read timed out"))
                .build()));
    assertTrue(
        isRemoteFailure(
            new RuntimeException(
                "wrapped", StsException.builder().message("unavailable").statusCode(503).build())));
  }

  @Test
  public void testNothingOnTheHostIsNotARemoteFailure() {
    assertFalse(isRemoteFailure(new RuntimeException("AWS_ROLE_ARN: blank variable value.")));
    assertFalse(
        isRemoteFailure(
            new RuntimeException("Fetching Web Identity token failed: File does not exist")));
    assertFalse(
        isRemoteFailure(
            SdkClientException.builder()
                .message("Profile file contained no credentials for profile 'default'")
                .build()));
    assertFalse(
        isRemoteFailure(
            SdkClientException.builder()
                .message(
                    "Cannot fetch credentials from container - neither"
                        + " AWS_CONTAINER_CREDENTIALS_FULL_URI or"
                        + " AWS_CONTAINER_CREDENTIALS_RELATIVE_URI environment variables are set.")
                .build()));
    assertFalse(
        isRemoteFailure(
            new IllegalStateException(
                "To use Sso related properties in the 'default' profile, the 'sso' service module"
                    + " must be on the class path.")));
  }
}

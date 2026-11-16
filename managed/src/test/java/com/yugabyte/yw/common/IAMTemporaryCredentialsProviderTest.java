// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.isRetryableFailure;
import static com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.resultWithin;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.SocketTimeoutException;
import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
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
    // STS could not reach the token's identity provider, which AWS documents as often transient.
    assertTrue(isRetryableFailure(stsReply(400, "IDPCommunicationError")));
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

  /**
   * The off-AWS check runs on request threads, so a check that hangs is given up on, and so is one
   * the shared pool has no room for.
   */
  @Test
  public void testResultWithinGivesUpOnWorkThatTakesTooLongOrHasNoRoom() throws Exception {
    ThreadPoolExecutor executor =
        new ThreadPoolExecutor(0, 1, 1, TimeUnit.MINUTES, new SynchronousQueue<>());
    try {
      assertEquals(Optional.of(true), resultWithin(() -> true, Duration.ofSeconds(5), executor));
      assertThrows(
          IllegalStateException.class,
          () ->
              resultWithin(
                  () -> {
                    throw new IllegalStateException("failed");
                  },
                  Duration.ofSeconds(5),
                  executor));

      CountDownLatch release = new CountDownLatch(1);
      Callable<Boolean> hangs =
          () -> {
            release.await();
            return true;
          };
      long start = System.nanoTime();
      assertEquals(Optional.empty(), resultWithin(hangs, Duration.ofMillis(100), executor));
      assertTrue(Duration.ofNanos(System.nanoTime() - start).compareTo(Duration.ofSeconds(5)) < 0);

      // Once the timed-out work has seen its interrupt and freed the pool's one thread, hold that
      // thread busy: the next check then finds no room.
      while (executor.getActiveCount() > 0) {
        Thread.sleep(10);
      }
      CountDownLatch busy = new CountDownLatch(1);
      executor.submit(
          () -> {
            busy.countDown();
            release.await();
            return null;
          });
      busy.await();
      assertThrows(
          RejectedExecutionException.class,
          () -> resultWithin(() -> true, Duration.ofSeconds(5), executor));
      release.countDown();
    } finally {
      executor.shutdownNow();
    }
  }

  @Test
  public void testNothingOnTheHostIsNotRetryable() {
    // The host's own I/O, outside any call to AWS.
    assertFalse(isRetryableFailure(new UncheckedIOException(new IOException("Permission denied"))));
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

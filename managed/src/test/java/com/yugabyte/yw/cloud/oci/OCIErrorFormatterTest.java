// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.oracle.bmc.ServiceDetails;
import com.oracle.bmc.model.BmcException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.util.List;
import java.util.Map;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;

public class OCIErrorFormatterTest {

  private static final String REQUEST_ID = "test-opc-request-id";
  private static final ServiceDetails LIST_KEYS =
      new ServiceDetails(
          "KeyManagement",
          "ListKeys",
          "https://management.kms.us-sanjose-1.oci.oraclecloud.com/20180608/keys?compartmentId=x",
          "https://docs.oracle.com/iaas/api/#/en/key/release/KeySummary/ListKeys");

  private static BmcException serviceError(
      int status, String code, String message, String originalMessage, Map<String, String> extra) {
    return new BmcException(
        status, code, message, REQUEST_ID, LIST_KEYS, originalMessage, null, null, extra);
  }

  private static BmcException invalidCompartment() {
    return serviceError(400, "InvalidParameter", "The compartmentId must be an ocid.", null, null);
  }

  private static String userMessage(Throwable t) {
    String message = OCIErrorFormatter.userMessage(t);
    for (String boilerplate :
        List.of("Timestamp:", "Client version:", "Troubleshooting Tips", "docs.oracle.com")) {
      assertFalse(message, message.contains(boilerplate));
    }
    return message;
  }

  @Test
  public void testServiceErrorKeepsMessageAndCode() {
    assertEquals(
        "OCI KeyManagement ListKeys failed: The compartmentId must be an ocid."
            + " (400 InvalidParameter)",
        userMessage(invalidCompartment()));
  }

  @Test
  public void testDetailUsedWhenMessageMissing() {
    BmcException e =
        serviceError(400, "InvalidParameter", null, null, Map.of("detail", "Bucket is elsewhere"));
    assertEquals(
        "OCI KeyManagement ListKeys failed: Bucket is elsewhere (400 InvalidParameter)",
        userMessage(e));
  }

  @Test
  public void testDetailAppendedWhenDifferentFromMessage() {
    BmcException e =
        serviceError(400, "InvalidParameter", "Bad request.", null, Map.of("detail", "Too long"));
    assertEquals(
        "OCI KeyManagement ListKeys failed: Bad request. Detail: Too long (400 InvalidParameter)",
        userMessage(e));
  }

  @Test
  public void testOriginalMessageUsedWhenMessageMissing() {
    BmcException e = serviceError(409, "IncorrectState", null, "Key is being deleted.", null);
    assertEquals(
        "OCI KeyManagement ListKeys failed: Key is being deleted. (409 IncorrectState)",
        userMessage(e));
  }

  @Test
  public void testMissingMessageStillShowsStatusAndCode() {
    assertEquals(
        "OCI KeyManagement ListKeys failed: no error message in the response (400"
            + " MissingParameter)",
        userMessage(serviceError(400, "MissingParameter", null, null, null)));
  }

  @Test
  public void testNotFoundIncludesEndpointHost() {
    BmcException e =
        serviceError(
            404,
            "NotAuthorizedOrNotFound",
            "Authorization failed or requested resource not found.",
            null,
            null);
    assertEquals(
        "OCI KeyManagement ListKeys failed: Authorization failed or requested resource not found."
            + " (404 NotAuthorizedOrNotFound, endpoint:"
            + " management.kms.us-sanjose-1.oci.oraclecloud.com)",
        userMessage(e));
  }

  @Test
  public void testServerErrorIncludesRequestId() {
    BmcException e = serviceError(500, "InternalServerError", "Internal error.", null, null);
    assertEquals(
        "OCI KeyManagement ListKeys failed: Internal error. (500 InternalServerError)."
            + " OCI request ID: "
            + REQUEST_ID,
        userMessage(e));
  }

  @Test
  public void testClientSideErrorIncludesRootCause() {
    BmcException e =
        BmcException.createClientSide(
            "Processing exception while communicating to: https://kms.bad-region.oraclecloud.com",
            new RuntimeException(new UnknownHostException("kms.bad-region.oraclecloud.com")),
            REQUEST_ID,
            LIST_KEYS);
    assertEquals(
        "OCI KeyManagement ListKeys failed: Processing exception while communicating to:"
            + " https://kms.bad-region.oraclecloud.com."
            + " Cause: UnknownHostException: kms.bad-region.oraclecloud.com",
        userMessage(e));
  }

  @Test
  public void testClientSideRootCauseNotRepeated() {
    String hostError = "kms.bad-region.oraclecloud.com: Name or service not known";
    BmcException e =
        new BmcException(
            true,
            "javax.ws.rs.ProcessingException: java.net.UnknownHostException: " + hostError,
            new RuntimeException(new UnknownHostException(hostError)),
            REQUEST_ID);
    assertEquals(
        "OCI request failed: javax.ws.rs.ProcessingException: java.net.UnknownHostException: "
            + hostError,
        userMessage(e));
  }

  @Test
  public void testTimeoutWithoutServiceDetails() {
    BmcException e =
        new BmcException(
            true, "Timed out", new SocketTimeoutException("Read timed out"), REQUEST_ID);
    assertEquals(
        "OCI request failed: Timed out. Cause: SocketTimeoutException: Read timed out",
        userMessage(e));
  }

  @Test
  public void testUnknownServiceDetailsAreOmitted() {
    BmcException e =
        new BmcException(
            400, "InvalidParameter", "Bad.", REQUEST_ID, ServiceDetails.UNKNOWN_SERVICE_DETAILS);
    assertEquals("OCI request failed: Bad. (400 InvalidParameter)", userMessage(e));
  }

  @Test
  public void testNonOciExceptionIsUnchanged() {
    IllegalStateException cause = new IllegalStateException("boom");
    assertEquals("boom", OCIErrorFormatter.userMessage(cause));
    RuntimeException wrapper = new RuntimeException(cause);
    assertEquals(wrapper.getMessage(), OCIErrorFormatter.userMessage(wrapper));
    assertNull(OCIErrorFormatter.userMessage(new NullPointerException()));
    assertEquals("Lookup failed: boom", OCIErrorFormatter.logAndShorten("Lookup failed", cause));
    assertEquals("Location a.: boom", OCIErrorFormatter.userMessage("Location a.", cause));
    assertFalse(OCIErrorFormatter.isOciError(wrapper));
    assertTrue(OCIErrorFormatter.isOciError(new RuntimeException(invalidCompartment())));
  }

  @Test
  public void testUserMessageWithContextMatchesLogAndShorten() {
    RuntimeException wrapped = new RuntimeException("Key lookup failed", invalidCompartment());
    assertEquals(
        OCIErrorFormatter.logAndShorten("Create failed", wrapped),
        OCIErrorFormatter.userMessage("Create failed", wrapped));
  }

  @Test
  public void testWrapperThatOnlyRepeatsCauseIsSkipped() {
    RuntimeException wrapped = new RuntimeException(new RuntimeException(invalidCompartment()));
    assertEquals(userMessage(invalidCompartment()), userMessage(wrapped));
  }

  @Test
  public void testWrapperThatEmbedsSdkMessageKeepsItsPrefix() {
    BmcException e = invalidCompartment();
    RuntimeException wrapped = new RuntimeException("Validation failed: " + e, e);
    assertEquals("Validation failed: " + userMessage(e), userMessage(wrapped));
    wrapped = new RuntimeException("Validation failed: " + e.getMessage(), e);
    assertEquals("Validation failed: " + userMessage(e), userMessage(wrapped));
  }

  @Test
  public void testWrapperTextAfterEmbeddedSdkMessageIsKept() {
    BmcException e = invalidCompartment();
    RuntimeException wrapped =
        new RuntimeException("Validation failed: " + e.getMessage() + ". Check the bucket.", e);
    assertEquals(
        "Validation failed: " + userMessage(e) + ". Check the bucket.", userMessage(wrapped));
  }

  @Test
  public void testEmbeddedSdkMessageIsReplacedFromFirstToLastLine() {
    String sameFirstAndLast = "Error.\nTimestamp: 2026-10-20T10:00:00Z\nError.";
    for (String sdkMessage : List.of(sameFirstAndLast, "Error.")) {
      BmcException e =
          new BmcException(400, "InvalidParameter", "Bad.", REQUEST_ID, LIST_KEYS) {
            @Override
            public String getMessage() {
              return sdkMessage;
            }
          };
      assertEquals(
          "Wrapped: " + userMessage(e) + " Retry.",
          userMessage(new RuntimeException("Wrapped: " + sdkMessage + " Retry.", e)));
    }
  }

  @Test
  public void testPartiallyEmbeddedSdkMessageIsKeptInFull() {
    BmcException e = invalidCompartment();
    String partial = StringUtils.substringBefore(e.getMessage(), "Client version:");
    assertEquals(
        partial + ": " + userMessage(e),
        OCIErrorFormatter.userMessage(new RuntimeException(partial, e)));
  }

  @Test
  public void testWrapperWithOwnMessageGetsOciTextAppended() {
    BmcException e = invalidCompartment();
    assertEquals(
        "Key lookup failed: " + userMessage(e),
        userMessage(new RuntimeException("Key lookup failed", e)));
    assertEquals(
        "Invalid OCI credentials. " + userMessage(e),
        userMessage(new RuntimeException("Invalid OCI credentials.", e)));
  }

  @Test
  public void testAlreadyFormattedWrapperIsNotFormattedAgain() {
    BmcException e = invalidCompartment();
    RuntimeException wrapped =
        new RuntimeException(OCIErrorFormatter.logAndShorten("Key lookup failed", e), e);
    assertEquals("Key lookup failed: " + userMessage(e), userMessage(wrapped));
    assertEquals(
        "Create failed: Key lookup failed: " + userMessage(e),
        OCIErrorFormatter.logAndShorten("Create failed", wrapped));
  }

  @Test
  public void testContextFromEveryWrapperIsKept() {
    BmcException e = invalidCompartment();
    RuntimeException vaultCheck =
        new RuntimeException(OCIErrorFormatter.userMessage("OCI vault is not accessible.", e), e);
    assertEquals(
        "Error refreshing OCI KMS config 'x'. OCI vault is not accessible. " + userMessage(e),
        userMessage(new RuntimeException("Error refreshing OCI KMS config 'x'.", vaultCheck)));

    RuntimeException embedded = new RuntimeException("Key lookup failed: " + e.getMessage(), e);
    assertEquals(
        "Create failed: Key lookup failed: " + userMessage(e),
        userMessage(new RuntimeException("Create failed", embedded)));
    assertEquals(
        "Create failed: Key lookup failed: " + userMessage(e) + ". Retry.",
        userMessage(
            new RuntimeException(
                "Create failed: " + embedded.getMessage() + ". Retry.", embedded)));
  }

  @Test
  public void testEndpointHost() {
    assertEquals(
        "iaas.us-ashburn-1.oraclecloud.com",
        OCIErrorFormatter.endpointHost("GET https://iaas.us-ashburn-1.oraclecloud.com/20160918/x"));
    assertEquals(
        "objectstorage.example.com",
        OCIErrorFormatter.endpointHost("https://objectstorage.example.com?a=b"));
    assertEquals("host", OCIErrorFormatter.endpointHost("https://host"));
    assertNull(OCIErrorFormatter.endpointHost("not a url"));
    assertNull(OCIErrorFormatter.endpointHost(null));
  }
}

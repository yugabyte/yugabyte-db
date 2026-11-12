package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import org.junit.Test;

// HELM_VALUES redaction reads telemetry provider credentials from the DB.
public class RedactingServiceShellOutputTest extends FakeDBApplication {

  @Test
  public void testShellOutputRedactsCertKeysAndSasSignaturesOnEachLine() {
    String certKey =
        Base64.getEncoder().encodeToString("dummy private key".getBytes(StandardCharsets.UTF_8));
    String helmOutput =
        "master:\n"
            + "  tolerations:\n"
            + "  - key: dedicated\n"
            + "tls:\n"
            + "  nodeCert:\n"
            + "    key: "
            + certKey
            + "\n";
    String redacted =
        RedactingService.redactShellProcessOutput(helmOutput, RedactionTarget.HELM_VALUES);
    assertFalse(redacted, redacted.contains(certKey));
    assertTrue(redacted, redacted.contains("  - key: dedicated\n"));

    assertEquals(
        "https://example.com/c?sv=1&sig=" + SECRET_REPLACEMENT + "\nnext line\n",
        RedactingService.redactShellProcessOutput(
            "https://example.com/c?sv=1&sig=dummySig\nnext line\n", RedactionTarget.QUERY_PARAMS));
  }
}

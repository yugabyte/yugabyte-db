package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;

import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import org.junit.Test;
import org.mockito.MockedStatic;
import org.mockito.Mockito;

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

  @Test
  public void testShellOutputIsHiddenWhenRedactionFails() {
    try (MockedStatic<HbaConfRedactor> redactor =
        Mockito.mockStatic(HbaConfRedactor.class, Mockito.CALLS_REAL_METHODS)) {
      redactor
          .when(() -> HbaConfRedactor.redactFreeText(anyString()))
          .thenThrow(new RuntimeException());
      assertEquals(
          SECRET_REPLACEMENT,
          RedactingService.redactShellProcessOutput(
              "ycql_ldap_bind_passwd: dummySecret\nnext line\n", RedactionTarget.QUERY_PARAMS));
    }
  }

  @Test
  public void testShellOutputKeepsTheLinesAfterAnUnclosedValue() {
    assertEquals(
        "run --ycql_ldap_bind_passwd, " + SECRET_REPLACEMENT + "\nerror: next line\n",
        RedactingService.redactShellProcessOutput(
            "run --ycql_ldap_bind_passwd dummySecret\nerror: next line\n",
            RedactionTarget.QUERY_PARAMS));
  }

  @Test
  public void testRedactRootCaKeysTakesTheLastKeyWithAValue() {
    String key = Base64.getEncoder().encodeToString("dummy key".getBytes(StandardCharsets.UTF_8));
    assertEquals(
        "  key: " + SECRET_REPLACEMENT, RedactingService.redactrootCAKeys("  key: " + key));
    assertEquals(
        "tls.key: " + SECRET_REPLACEMENT + "  # node",
        RedactingService.redactrootCAKeys("tls.key: \t" + key + "  # node"));
    assertEquals(
        "a key: x key: " + SECRET_REPLACEMENT,
        RedactingService.redactrootCAKeys("a key: x key: " + key));
    assertEquals(
        "key: " + SECRET_REPLACEMENT + " key:",
        RedactingService.redactrootCAKeys("key: " + key + " key:"));
    // Not base64, or no value after the last key: with one.
    assertEquals("  - key: dedicated", RedactingService.redactrootCAKeys("  - key: dedicated"));
    assertEquals("key:" + key, RedactingService.redactrootCAKeys("key:" + key));
  }

  // About 900 KB. The regex this replaced took 20 s on 64 KB of this; on this it would take hours.
  @Test(timeout = 10_000)
  public void testRedactRootCaKeysIsLinearInALongLine() {
    String line = "{a_key:[1,2]} ".repeat(1 << 16);
    assertEquals(line, RedactingService.redactrootCAKeys(line));
    assertEquals(
        "data:\n  dashboard: '" + line + "'\n",
        RedactingService.redactShellProcessOutput(
            "data:\n  dashboard: '" + line + "'\n", RedactionTarget.HELM_VALUES));
  }
}

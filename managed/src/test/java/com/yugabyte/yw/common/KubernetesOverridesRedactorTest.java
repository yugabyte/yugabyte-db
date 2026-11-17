// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.util.Map;
import org.junit.Test;
import org.yaml.snakeyaml.Yaml;

public class KubernetesOverridesRedactorTest {

  private static final String STORED =
      String.join(
          "\n",
          "# LDAP for YCQL and YSQL",
          "gflags:",
          "  tserver:",
          "    ycql_ldap_bind_passwd: 'tserver-secret'",
          "    ysql_hba_conf_csv: >-",
          "      \"host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com",
          "      ldapbindpasswd=hba-secret\"",
          "  master:",
          "    ycql_ldap_bind_passwd: \"master-secret\"",
          "tserver:",
          "  podLabels:",
          "    env: test",
          "");

  private static void assertNoSecrets(String text) {
    for (String secret : new String[] {"master-secret", "tserver-secret", "hba-secret"}) {
      assertFalse(text, text.contains(secret));
    }
  }

  @SuppressWarnings("unchecked")
  private static Object at(String yaml, String... path) {
    Object node = new Yaml().load(yaml);
    for (String key : path) {
      node = ((Map<String, Object>) node).get(key);
    }
    return node;
  }

  @Test
  public void redactKeepsEverythingButTheSecrets() {
    String redacted = KubernetesOverridesRedactor.redact(STORED);
    assertNoSecrets(redacted);
    assertTrue(redacted, redacted.startsWith("# LDAP for YCQL and YSQL\n"));
    assertEquals("REDACTED", at(redacted, "gflags", "tserver", "ycql_ldap_bind_passwd"));
    assertEquals(
        "\"host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapbindpasswd=REDACTED\"",
        at(redacted, "gflags", "tserver", "ysql_hba_conf_csv"));
    // The block scalar's line break is kept, so the next key stays a sibling.
    assertEquals("REDACTED", at(redacted, "gflags", "master", "ycql_ldap_bind_passwd"));
    assertEquals("test", at(redacted, "tserver", "podLabels", "env"));
  }

  @Test
  public void redactCountsCodePoints() {
    String overrides = "# 😀 é\ngflags:\n  tserver:\n    ycql_ldap_bind_passwd: tserver-secret\n";
    assertEquals(
        "# 😀 é\ngflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\n",
        KubernetesOverridesRedactor.redact(overrides));
  }

  @Test
  public void redactFallsBackToTextScanForUnparsableYaml() {
    String overrides = "gflags: [\n  ycql_ldap_bind_passwd: tserver-secret\n";
    assertNoSecrets(KubernetesOverridesRedactor.redact(overrides));
  }

  @Test
  public void redactLeavesOverridesWithoutSecretsAlone() {
    String overrides = "tserver:\n  podLabels:\n    env: test # REDACTED\n";
    assertEquals(overrides, KubernetesOverridesRedactor.redact(overrides));
    assertNull(KubernetesOverridesRedactor.redact(null));
  }

  @Test
  public void restoreReturnsStoredTextForUnchangedOverrides() {
    String submitted = KubernetesOverridesRedactor.redact(STORED);
    assertEquals(STORED, KubernetesOverridesRedactor.restore(STORED, submitted, "overrides"));
  }

  @Test
  public void restoreKeepsOtherChanges() {
    String submitted = KubernetesOverridesRedactor.redact(STORED).replace("env: test", "env: prod");
    String restored = KubernetesOverridesRedactor.restore(STORED, submitted, "overrides");
    assertEquals("tserver-secret", at(restored, "gflags", "tserver", "ycql_ldap_bind_passwd"));
    assertEquals("master-secret", at(restored, "gflags", "master", "ycql_ldap_bind_passwd"));
    assertEquals(
        at(STORED, "gflags", "tserver", "ysql_hba_conf_csv"),
        at(restored, "gflags", "tserver", "ysql_hba_conf_csv"));
    assertEquals("prod", at(restored, "tserver", "podLabels", "env"));
    assertFalse(restored, restored.contains(RedactingService.SECRET_REPLACEMENT));
  }

  @Test
  public void restoreKeepsSubmittedRealValues() {
    String submitted =
        KubernetesOverridesRedactor.redact(STORED)
            .replaceFirst("ycql_ldap_bind_passwd: REDACTED", "ycql_ldap_bind_passwd: new");
    String restored = KubernetesOverridesRedactor.restore(STORED, submitted, "overrides");
    assertEquals("new", at(restored, "gflags", "tserver", "ycql_ldap_bind_passwd"));
    assertEquals("master-secret", at(restored, "gflags", "master", "ycql_ldap_bind_passwd"));
  }

  @Test
  public void restoreRejectsRedactedWithNothingStoredAtThatPath() {
    String submitted = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\n";
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> KubernetesOverridesRedactor.restore(null, submitted, "overrides"));
    assertTrue(e.getMessage(), e.getMessage().contains("gflags.tserver.ycql_ldap_bind_passwd"));
    assertThrows(
        PlatformServiceException.class,
        () ->
            KubernetesOverridesRedactor.restore(
                "gflags:\n  master:\n    ycql_ldap_bind_passwd: master-secret\n",
                submitted,
                "overrides"));
  }

  @Test
  public void restoreRejectsRedactedForANewAz() {
    String submitted = KubernetesOverridesRedactor.redact(STORED);
    assertThrows(
        PlatformServiceException.class,
        () ->
            KubernetesOverridesRedactor.restoreAz(
                Map.of("az-1", STORED), Map.of("az-1", submitted, "az-2", submitted)));
    assertEquals(
        Map.of("az-1", STORED),
        KubernetesOverridesRedactor.restoreAz(Map.of("az-1", STORED), Map.of("az-1", submitted)));
  }

  @Test
  public void restoreRejectsHbaRedactedWithOtherFieldsChanged() {
    String submitted =
        KubernetesOverridesRedactor.redact(STORED)
            .replace("ldapserver=ldap.example.com", "ldapserver=ldap2.example.com");
    assertThrows(
        PlatformServiceException.class,
        () -> KubernetesOverridesRedactor.restore(STORED, submitted, "overrides"));
  }

  @Test
  public void restoreRejectsRedactedStoredValue() {
    String stored = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\n";
    String submitted = stored + "tserver:\n  podLabels:\n    env: test\n";
    assertThrows(
        PlatformServiceException.class,
        () -> KubernetesOverridesRedactor.restore(stored, submitted, "overrides"));
  }

  @Test
  public void restoreIgnoresRedactedOutsideSecrets() {
    String submitted = "tserver:\n  podLabels:\n    note: REDACTED\n";
    assertEquals(submitted, KubernetesOverridesRedactor.restore(null, submitted, "overrides"));
  }
}

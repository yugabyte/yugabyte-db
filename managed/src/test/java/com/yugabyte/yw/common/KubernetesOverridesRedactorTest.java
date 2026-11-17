// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.UUID;
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
  public void redactHidesUnparsableTextThatMentionsASecret() {
    String overrides = "gflags: [\n  ycql_ldap_bind_passwd: tserver-secret\n";
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(overrides));
    // Sent back unchanged, it restores the stored text.
    assertEquals(overrides, KubernetesOverridesRedactor.restore(overrides, "REDACTED", "o"));
  }

  @Test
  public void redactHidesUnparsableTextWithABackslash() {
    // A quoted key can spell a secret name with escapes, which only a parse would reveal.
    String escapedKey =
        "gflags:\n  tserver:\n    \"\\x79cql_ldap_bind_passwd\": tserver-secret\n\tbad: indent\n";
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(escapedKey));
    // "\q" is not a YAML escape.
    String badEscape = "tserver:\n  podLabels:\n    path: \"C:\\q\"\n";
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(badEscape));
    assertEquals(badEscape, KubernetesOverridesRedactor.restore(badEscape, "REDACTED", "o"));
  }

  @Test
  public void redactLeavesOverridesWithoutSecretsAlone() {
    String overrides = "tserver:\n  podLabels:\n    env: test # REDACTED\n";
    assertEquals(overrides, KubernetesOverridesRedactor.redact(overrides));
    String anchored = "base: &b {path: \"C:\\\\x\"}\ntserver: *b\n";
    assertEquals(anchored, KubernetesOverridesRedactor.redact(anchored));
    String hbaList = "gflags:\n  ysql_hba_conf_csv: [local all all trust]\nx: \"C:\\\\x\"\n";
    assertEquals(hbaList, KubernetesOverridesRedactor.redact(hbaList));
    assertNull(KubernetesOverridesRedactor.redact(null));
  }

  @Test
  public void redactKeepsEmptySecretValues() {
    String overrides = "gflags:\n  ycql_ldap_bind_passwd:\n  ldapBindPassword: \"\"\nx: y\n";
    assertEquals(overrides, KubernetesOverridesRedactor.redact(overrides));
  }

  @Test
  public void redactCoversEverySecretName() {
    for (String name : RedactingService.sensitiveNames(null, null)) {
      assertEquals(
          name,
          "gflags:\n  " + name + ": REDACTED\n",
          KubernetesOverridesRedactor.redact("gflags:\n  " + name + ": tserver-secret\n"));
    }
  }

  @Test
  public void redactCoversFlowStyle() {
    String stored =
        "gflags: {tserver: {ycql_ldap_bind_passwd: tserver-secret, ysql_hba_conf_csv: \"host all"
            + " all 0.0.0.0/0 ldap ldapbindpasswd=hba-secret\", x: 1}}\n";
    String redacted = KubernetesOverridesRedactor.redact(stored);
    assertNoSecrets(redacted);
    assertEquals("REDACTED", at(redacted, "gflags", "tserver", "ycql_ldap_bind_passwd"));
    assertEquals(
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=REDACTED",
        at(redacted, "gflags", "tserver", "ysql_hba_conf_csv"));
    assertEquals(1, at(redacted, "gflags", "tserver", "x"));
    assertEquals(stored, KubernetesOverridesRedactor.restore(stored, redacted, "o"));
  }

  @Test
  public void redactHidesOverLongTextThatCouldHoldASecret() {
    String filler = "x: \"" + "a".repeat(3_200_000) + "\"\n";
    assertEquals(
        "REDACTED",
        KubernetesOverridesRedactor.redact(filler + "ycql_ldap_bind_passwd: tserver-secret\n"));
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(filler + "y: \"\\t\"\n"));
    assertEquals(filler, KubernetesOverridesRedactor.redact(filler));
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
    assertTrue(e.getMessage(), e.getMessage().contains("nothing is stored at the same place"));
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

  private static String base64(String text) {
    return Base64.getEncoder().encodeToString(text.getBytes(StandardCharsets.UTF_8));
  }

  private static final String SECRET_NAME_BASE64 = base64("ycql_ldap_bind_passwd");

  private static final List<String> HIDDEN_WHOLE =
      List.of(
          // A binary tag, which Helm decodes from base64, in each spelling.
          "gflags:\n  tserver:\n    !!binary " + SECRET_NAME_BASE64 + ": master-secret\n",
          "gflags:\n  tserver:\n    !<tag:yaml.org,2002:binary> "
              + SECRET_NAME_BASE64
              + ": master-secret\n",
          "%TAG !e! tag:yaml.org,2002:\n---\ngflags:\n  tserver:\n    !e!binary "
              + SECRET_NAME_BASE64
              + ": master-secret\n",
          // Anchors, aliases and tags.
          "gflags:\n  master:\n    ycql_ldap_bind_passwd: &pw master-secret\n"
              + "  tserver:\n    ycql_ldap_bind_passwd: *pw\n",
          "shared: &pw master-secret\ngflags:\n  master:\n    ycql_ldap_bind_passwd: *pw\n",
          "base: &b {ycql_ldap_bind_passwd: master-secret}\ngflags:\n  tserver: *b\n",
          "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: !!str master-secret\n",
          "x: &a 1\ngflags:\n  tserver:\n    ycql_ldap_bind_passwd: master-secret\n",
          "gflags:\n  note: &a # ycql_ldap_bind_passwd: master-secret\n    value\n",
          // Keys that aren't strings.
          "? [a, b]\n: c\ngflags:\n  tserver:\n    ycql_ldap_bind_passwd: master-secret\n",
          "? [a]\n: \"--ycql_ldap_bind_passwd=master-secret\"\n",
          // A key or other string that mentions a secret: in any form, more than once, or with the
          // value in the next item.
          "gflags:\n  \"ycql_ldap_bind_passwd=master-secret\": x\n",
          "gflags:\n  note: ycql_ldap_bind_passwd master-secret\n",
          "note: \"ycql_ldap_bind_passwd=tserver-secret, ycql_ldap_bind_passwd master-secret\"\n",
          "args: [\"--ycql_ldap_bind_passwd\", master-secret]\n",
          "note: \"--ysql_hba_conf_csv=host all all 0.0.0.0/0 ldap"
              + " ldapbindpasswd=master-secret\"\n",
          // A repeated secret key, and a secret that isn't a scalar.
          "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: master-secret\n"
              + "    ycql_ldap_bind_passwd: tserver-secret\n",
          "gflags:\n  tserver:\n    ycql_ldap_bind_passwd:\n      nested: master-secret\n",
          "gflags:\n  ysql_hba_conf_csv:\n    - \"host all all 0.0.0.0/0 ldap"
              + " ldapbindpasswd=master-secret\"\n");

  @Test
  public void redactHidesOverridesItCannotRewriteWhole() {
    String edited = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\nx: y\n";
    for (String stored : HIDDEN_WHOLE) {
      assertEquals(stored, "REDACTED", KubernetesOverridesRedactor.redact(stored));
      // Sent back unchanged, they restore the stored text; an edit can't restore from them.
      assertEquals(stored, KubernetesOverridesRedactor.restore(stored, "REDACTED", "o"));
      PlatformServiceException e =
          assertThrows(
              stored,
              PlatformServiceException.class,
              () -> KubernetesOverridesRedactor.restore(stored, edited, "o"));
      assertTrue(e.getMessage(), e.getMessage().contains("stored overrides cannot be read"));
      assertNoSecrets(e.getMessage());
    }
  }

  @Test
  public void redactHidesOverridesWithABinaryTag() {
    String flag =
        "tserver:\n  args: !!binary " + base64("--ycql_ldap_bind_passwd=master-secret") + "\n";
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(flag));
    String unparsable =
        "gflags:\n  tserver:\n    !!binary "
            + SECRET_NAME_BASE64
            + ": tserver-secret\n\tbad: indent\n";
    assertEquals("REDACTED", KubernetesOverridesRedactor.redact(unparsable));
    // Other tags load the text as written, and a ! elsewhere is only text.
    String str = "tserver:\n  podLabels:\n    env: !!str test\n    note: \"Hello!\"\n";
    assertEquals(str, KubernetesOverridesRedactor.redact(str));
  }

  @Test
  public void restoreRejectsRedactedInOverridesItCannotRewrite() {
    for (String submitted : HIDDEN_WHOLE) {
      String withRedacted =
          submitted.replace("master-secret", "REDACTED").replace("tserver-secret", "REDACTED");
      PlatformServiceException e =
          assertThrows(
              withRedacted,
              PlatformServiceException.class,
              () -> KubernetesOverridesRedactor.restore(STORED, withRedacted, "o"));
      assertTrue(e.getMessage(), e.getMessage().contains("cannot be restored into them"));
    }
  }

  @Test
  public void restoreRejectsOverridesThatAreOnlyRedacted() {
    for (String stored : new String[] {null, STORED}) {
      PlatformServiceException e =
          assertThrows(
              PlatformServiceException.class,
              () -> KubernetesOverridesRedactor.restore(stored, "REDACTED\n", "o"));
      assertTrue(e.getMessage(), e.getMessage().contains("submit them in full"));
    }
  }

  @Test
  public void restoreRejectsRedactedUnderAListItem() {
    String stored =
        "users:\n  - ycql_ldap_bind_passwd: tserver-secret\n"
            + "  - ycql_ldap_bind_passwd: master-secret\n";
    String redacted = KubernetesOverridesRedactor.redact(stored);
    assertNoSecrets(redacted);
    assertEquals(stored, KubernetesOverridesRedactor.restore(stored, redacted, "o"));
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> KubernetesOverridesRedactor.restore(stored, redacted + "x: y\n", "o"));
    assertTrue(e.getMessage(), e.getMessage().contains("users.0.ycql_ldap_bind_passwd"));
  }

  private static final String WITH_COMMENTS =
      String.join(
          "\n",
          "# ycql_ldap_bind_passwd: comment-secret",
          "gflags:",
          "  tserver:",
          "    ycql_ldap_bind_passwd: tserver-secret  # was ycql_ldap_bind_passwd: inline-secret",
          "  master: # ldapbindpasswd=key-comment-secret",
          "    extra: |  # ycql_ldap_bind_passwd: header-secret",
          "      plain block content",
          "other:",
          "  ycql_ldap_bind_passwd: 'other-secret'",
          "");

  @Test
  public void redactCoversComments() {
    String redacted = KubernetesOverridesRedactor.redact(WITH_COMMENTS);
    for (String secret :
        List.of(
            "comment-secret",
            "tserver-secret",
            "inline-secret",
            "key-comment-secret",
            "header-secret",
            "other-secret")) {
      assertFalse(redacted, redacted.contains(secret));
    }
    assertEquals("plain block content\n", at(redacted, "gflags", "master", "extra"));
    assertEquals("REDACTED", at(redacted, "other", "ycql_ldap_bind_passwd"));
  }

  @Test
  public void redactHidesTheCommentsBelowACommentThatMentionsASecret() {
    // A value can continue on the next line, as in commented-out YAML. Only blank space between
    // comments keeps them together; YAML content and comments above are kept.
    String overrides =
        String.join(
            "\n",
            "# LDAP settings",
            "# ldapbindpasswd:",
            "#   comment-secret",
            "",
            "  #   comment-secret",
            "x: y # kept",
            "z: 1 # ycql_ldap_bind_passwd=\"inline-secret",
            "#   inline-secret\"",
            "w: 2",
            "");
    assertEquals(
        String.join(
            "\n",
            "# LDAP settings",
            "# REDACTED",
            "# REDACTED",
            "",
            "  # REDACTED",
            "x: y # kept",
            "z: 1 # REDACTED",
            "# REDACTED",
            "w: 2",
            ""),
        KubernetesOverridesRedactor.redact(overrides));
    assertEquals("# REDACTED\n", KubernetesOverridesRedactor.redact("# ldapbindpasswd=x\n"));
  }

  @Test
  public void restorePutsBackSecretsButNotComments() {
    String submitted =
        KubernetesOverridesRedactor.redact(WITH_COMMENTS)
            + "tserver:\n  podLabels:\n    env: test\n";
    String restored = KubernetesOverridesRedactor.restore(WITH_COMMENTS, submitted, "overrides");
    assertEquals("tserver-secret", at(restored, "gflags", "tserver", "ycql_ldap_bind_passwd"));
    assertEquals("other-secret", at(restored, "other", "ycql_ldap_bind_passwd"));
    assertFalse(restored, restored.contains("comment-secret"));
  }

  @Test
  public void parseErrorsDoNotQuoteTheText() {
    // Overrides that can't be parsed, and text from them that SnakeYAML's messages would quote.
    Map<String, String> quotedByParser =
        Map.of(
            "ycql_ldap_bind_passwd: \"Xk9-secret\n", "Xk9-secret",
            "ycql_ldap_bind_passwd: *Xk9pass\n", "Xk9pass",
            "ycql_ldap_bind_passwd: !Xk9! x\n", "Xk9",
            "ycql_ldap_bind_passwd: \"\\xXk9\"\n", "Xk",
            "ycql_ldap_bind_passwd: \"\\UXk9pass12\"\n", "Xk9");
    String edited = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\nx: y\n";
    quotedByParser.forEach(
        (overrides, quoted) -> {
          String stored = "gflags:\n  tserver:\n    " + overrides;
          assertEquals(stored, "REDACTED", KubernetesOverridesRedactor.redact(stored));
          PlatformServiceException e =
              assertThrows(
                  stored,
                  PlatformServiceException.class,
                  () -> KubernetesOverridesRedactor.restore(stored, edited, "o"));
          assertTrue(e.getMessage(), e.getMessage().contains("cannot be parsed as YAML at line"));
          assertFalse(e.getMessage(), e.getMessage().contains(quoted));
          String submitted = stored + "y: REDACTED\n";
          e =
              assertThrows(
                  submitted,
                  PlatformServiceException.class,
                  () -> KubernetesOverridesRedactor.restore(STORED, submitted, "o"));
          assertTrue(e.getMessage(), e.getMessage().contains("cannot be parsed as YAML at line"));
          assertFalse(e.getMessage(), e.getMessage().contains(quoted));
        });
  }

  @Test
  public void redactParsesKeysSpelledWithEscapes() {
    // "\x79cql_ldap_bind_passwd" is ycql_ldap_bind_passwd to the parser and to Helm.
    String stored = "gflags:\n  tserver:\n    \"\\x79cql_ldap_bind_passwd\": tserver-secret\n";
    String redacted = KubernetesOverridesRedactor.redact(stored);
    assertFalse(redacted, redacted.contains("tserver-secret"));
    assertEquals(
        stored,
        KubernetesOverridesRedactor.restore(stored, redacted + "x: y\n", "overrides")
            .replace("x: y\n", ""));
  }

  @Test
  public void restoreKeepsUnchangedOverridesWithAStoredRedactedSecret() {
    // REDACTED can be stored when the redaction was off at save time. Edits and upgrades send the
    // universe's own overrides back unchanged; that must not get a 400.
    String stored =
        "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: REDACTED\n"
            + "  master:\n    ycql_ldap_bind_passwd: master-secret\n";
    assertEquals(stored, KubernetesOverridesRedactor.restore(stored, stored, "overrides"));
    assertEquals(
        stored,
        KubernetesOverridesRedactor.restore(
            stored, KubernetesOverridesRedactor.redact(stored), "overrides"));
  }

  @Test
  public void restoreQuotesAPlainValueMovedIntoAFlowCollection() {
    String stored = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: pa,ss[1]\n";
    String submitted = "gflags: {tserver: {ycql_ldap_bind_passwd: REDACTED}}\nx: y\n";
    String restored = KubernetesOverridesRedactor.restore(stored, submitted, "o");
    assertEquals("pa,ss[1]", at(restored, "gflags", "tserver", "ycql_ldap_bind_passwd"));
  }

  @Test
  public void restoreTreatsAValueContainingRedactedAsRedacted() {
    String stored = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: tserver-secret\n";
    String submitted = "gflags:\n  tserver:\n    ycql_ldap_bind_passwd: \"REDACTED \"\nx: y\n";
    String restored = KubernetesOverridesRedactor.restore(stored, submitted, "o");
    assertEquals("tserver-secret", at(restored, "gflags", "tserver", "ycql_ldap_bind_passwd"));
  }

  @Test
  public void redactIsCachedByContent() {
    String text = STORED + "id: " + UUID.randomUUID() + "\n";
    String redacted = KubernetesOverridesRedactor.redact(text);
    assertNoSecrets(redacted);
    // Equal text from another response gets the cached result; changed text is redacted afresh.
    assertSame(redacted, KubernetesOverridesRedactor.redact(new String(text.toCharArray())));
    String changed = KubernetesOverridesRedactor.redact(text.replace("env: test", "env: prod"));
    assertEquals(redacted.replace("env: test", "env: prod"), changed);
  }
}

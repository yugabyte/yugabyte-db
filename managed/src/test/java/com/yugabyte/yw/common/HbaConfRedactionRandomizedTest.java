// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.gflags.GFlagsUtil.YSQL_HBA_CONF_CSV;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import org.junit.Test;
import play.libs.Json;

/**
 * Random ysql_hba_conf_csv values, each sent through every way YBA redacts one. Passwords start
 * with Zq and end with Qz, which nothing else contains, so any output holding either leaks.
 */
public class HbaConfRedactionRandomizedTest {

  private static final int[] ALPHABET =
      "aB9 \t\"#,='\\{}[]:;!$%&()*+-./<>?@^_`|~\u00e9\u5bc6\ud83d\udd10".codePoints().toArray();

  private final Random random = new Random(23624);

  private record Case(String value, String expected) {}

  @Test
  public void testEveryEncoding() {
    for (int i = 0; i < 2000; i++) {
      Case c = randomValue();
      String message = c.value();
      String value = c.value();
      String expected = c.expected();

      assertEquals(message, expected, HbaConfRedactor.redact(value));

      Map<String, String> gflags = new LinkedHashMap<>();
      gflags.put("max_log_size", "256");
      gflags.put(YSQL_HBA_CONF_CSV, value);
      assertEquals(
          message,
          expected,
          RedactingService.redactGFlags(gflags, null, null).get(YSQL_HBA_CONF_CSV));
      assertEquals(
          message,
          List.of("bin/yb-tserver", "--" + YSQL_HBA_CONF_CSV + "=" + expected),
          RedactingService.redactGFlagCommand(
              List.of("bin/yb-tserver", "--" + YSQL_HBA_CONF_CSV + "=" + value), null, null));

      String serverConf =
          "--placement_cloud=aws\n--" + YSQL_HBA_CONF_CSV + "=" + value + "\n--max_log_size=256\n";
      assertEquals(
          message,
          serverConf.replace(value, expected),
          RedactingService.redactSensitiveInfoInString(serverConf));

      ObjectNode universe = universeJson(value);
      for (boolean apiRedaction : new boolean[] {true, false}) {
        assertHbaValues(
            message,
            expected,
            RedactingService.filterSecretFields(
                universe, RedactionTarget.LOGS, null, null, apiRedaction));
      }
      assertHbaValues(
          message,
          expected,
          RedactingService.filterSecretFields(universe, RedactionTarget.APIS, null, null, true));

      ObjectNode taskDetails = Json.newObject();
      taskDetails.put("details", universe.toString());
      taskDetails.put("message", "Combined gflags: " + gflags);
      JsonNode redactedTask =
          RedactingService.filterSecretFields(taskDetails, RedactionTarget.LOGS, null, null, true);
      assertHbaValues(message, expected, Json.parse(redactedTask.get("details").asText()));
      assertNoPassword(message, redactedTask.toString());

      ObjectNode audit = Json.newObject();
      ObjectNode flag = audit.putObject("gflags").putArray("tserver").addObject();
      flag.put("name", YSQL_HBA_CONF_CSV);
      flag.put("old", value);
      flag.put("new", value);
      JsonNode redactedAudit = RedactingService.redactAuditAdditionalDetails(audit, null, null);
      assertEquals(message, expected, redactedAudit.at("/gflags/tserver/0/old").asText());
      assertEquals(message, expected, redactedAudit.at("/gflags/tserver/0/new").asText());

      String yaml =
          "gflags:\n  tserver:\n    "
              + YSQL_HBA_CONF_CSV
              + ": '"
              + value.replace("'", "''")
              + "'\n    max_log_size: 256\n";
      assertNoPassword(message, RedactingService.redactSensitiveInfoInString(yaml));
      assertNoPassword(message, RedactingService.redactSensitiveInfoInString(gflags.toString()));
      assertNoPassword(message, RedactingService.redactSensitiveInfoInString(universe.toString()));

      Map<String, String> merged =
          GFlagsUtil.mergeSensitiveGFlags(
              Map.of(YSQL_HBA_CONF_CSV, value), Map.of(YSQL_HBA_CONF_CSV, expected), null, null);
      assertEquals(message, value, merged.get(YSQL_HBA_CONF_CSV));
    }
  }

  /** A value of 1-3 records, and the same value with each password redacted. */
  private Case randomValue() {
    StringBuilder value = new StringBuilder();
    StringBuilder expected = new StringBuilder();
    int records = 1 + random.nextInt(3);
    for (int r = 0; r < records; r++) {
      if (r > 0) {
        value.append(',');
        expected.append(',');
      }
      String record = "local all yugabyte trust";
      String redacted = record;
      if (random.nextInt(4) > 0) {
        String password = password();
        boolean hbaQuote =
            password.chars().anyMatch(ch -> " \t\",#".indexOf(ch) >= 0) || random.nextBoolean();
        String head =
            "host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com"
                + (random.nextBoolean()
                    ? " ldapbasedn=\"dc=example,dc=com\""
                    : " ldapbasedn=dc=example")
                + (random.nextBoolean() ? " " : ",")
                + "ldapbindpasswd=";
        String tail = random.nextBoolean() ? " ldaptls=0" : "";
        record = head + (hbaQuote ? "\"" + password.replace("\"", "\"\"") + "\"" : password) + tail;
        redacted = head + (hbaQuote ? "\"REDACTED\"" : "REDACTED") + tail;
      }
      if (record.contains(",") || random.nextBoolean()) {
        record = "\"" + record.replace("\"", "\"\"") + "\"";
        redacted = "\"" + redacted.replace("\"", "\"\"") + "\"";
      }
      value.append(record);
      expected.append(redacted);
    }
    return new Case(value.toString(), expected.toString());
  }

  private String password() {
    StringBuilder password = new StringBuilder("Zq");
    int length = random.nextInt(12);
    for (int i = 0; i < length; i++) {
      password.appendCodePoint(ALPHABET[random.nextInt(ALPHABET.length)]);
    }
    return password.append("Qz").toString();
  }

  private static ObjectNode universeJson(String hbaConf) {
    ObjectNode universe = Json.newObject();
    ObjectNode userIntent =
        universe
            .putObject("universeDetails")
            .putArray("clusters")
            .addObject()
            .putObject("userIntent");
    userIntent.putObject("tserverGFlags").put(YSQL_HBA_CONF_CSV, hbaConf);
    userIntent
        .putObject("specificGFlags")
        .putObject("perProcessFlags")
        .putObject("value")
        .putObject("TSERVER")
        .put(YSQL_HBA_CONF_CSV, hbaConf);
    return universe;
  }

  private static void assertHbaValues(String message, String expected, JsonNode tree) {
    List<JsonNode> values = tree.findValues(YSQL_HBA_CONF_CSV);
    assertEquals(message, 2, values.size());
    for (JsonNode value : values) {
      assertEquals(message, expected, value.asText());
    }
    assertNoPassword(message, tree.toString());
  }

  private static void assertNoPassword(String message, String text) {
    assertFalse(message + "\n" + text, text.contains("Zq") || text.contains("Qz"));
  }
}

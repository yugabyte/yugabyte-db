// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;
import org.junit.Test;
import play.libs.Json;

/**
 * Redacts random ldapbindpasswd values in the forms YBA logs and returns them in, and restores them
 * from the redacted form.
 */
public class LdapBindPasswdHbaFormatRandomizedTest {

  private static final String HBA = GFlagsUtil.YSQL_HBA_CONF_CSV;
  private static final String ALNUM =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  // Postgres ends an unquoted value at whitespace or a comma, starts quoting at '"' and a comment
  // at '#', so an unquoted password holds none of them.
  private static final String UNQUOTED_PUNCT = "=\\{}[]':;!@$%^&*()-_+./<>?|~";
  private static final String QUOTED_PUNCT = UNQUOTED_PUNCT + " ,\"#";
  private static final String[] QUOTED_LOOKALIKES = {
    "\" ldapserver=x", "\"]", "\"}", "\",", "\\\"", "\"\"", " ldapbindpasswd=z"
  };
  private static final String[] OPTIONS_AFTER = {
    "",
    " ldapsearchattribute=cn",
    " clientcert=verify-full",
    " ldapsearchfilter=\"(uid=$username)\""
  };

  private final Random random = new Random(20181);

  private String password(boolean quoted) {
    StringBuilder password = new StringBuilder();
    for (int i = 8 + random.nextInt(13); i > 0; i--) {
      int kind = random.nextInt(10);
      if (kind < 5) {
        password.append(ALNUM.charAt(random.nextInt(ALNUM.length())));
      } else if (kind < 9 || !quoted) {
        String punct = quoted ? QUOTED_PUNCT : UNQUOTED_PUNCT;
        password.append(punct.charAt(random.nextInt(punct.length())));
      } else {
        password.append(QUOTED_LOOKALIKES[random.nextInt(QUOTED_LOOKALIKES.length)]);
      }
    }
    return password.toString();
  }

  private static String csvField(String record, boolean quote) {
    return quote || record.contains("\"") || record.contains(",")
        ? "\"" + record.replace("\"", "\"\"") + "\""
        : record;
  }

  /** An HBA CSV with LDAP records, and the same CSV with each password redacted. */
  private String[] hbaCsv() {
    List<String> fields = new ArrayList<>(List.of("local all yugabyte trust"));
    List<String> redactedFields = new ArrayList<>(fields);
    for (int i = 1 + random.nextInt(3); i > 0; i--) {
      boolean quoted = random.nextInt(3) > 0;
      String password = password(quoted);
      String after = OPTIONS_AFTER[random.nextInt(OPTIONS_AFTER.length)];
      String record =
          (after.contains("clientcert") ? "hostssl" : "host")
              + " all all 0.0.0.0/0 ldap ldapserver=ldap1.example.com"
              + (random.nextBoolean() ? " ldapbasedn=\"dc=example,dc=com\"" : "")
              + " ldapbinddn=cn=svc ldapbindpasswd=";
      boolean quoteField = random.nextBoolean();
      fields.add(
          csvField(
              record + (quoted ? "\"" + password.replace("\"", "\"\"") + "\"" : password) + after,
              quoteField));
      redactedFields.add(
          csvField(
              record + (quoted ? "\"" + SECRET_REPLACEMENT + "\"" : SECRET_REPLACEMENT) + after,
              quoteField));
    }
    if (random.nextBoolean()) {
      fields.add("host all all ::/0 md5");
      redactedFields.add("host all all ::/0 md5");
    }
    return new String[] {String.join(",", fields), String.join(",", redactedFields)};
  }

  private static void check(
      List<String> failures, String what, String input, String actual, String expected) {
    if (!expected.equals(actual)) {
      failures.add(
          what
              + "\n  input:    "
              + input
              + "\n  actual:   "
              + actual
              + "\n  expected: "
              + expected);
    }
  }

  @Test
  public void redactsAndRestoresRandomPasswords() {
    List<String> failures = new ArrayList<>();
    for (int i = 0; i < 5000 && failures.size() < 5; i++) {
      String[] csv = hbaCsv();
      String hba = csv[0];
      String redacted = csv[1];
      Map<String, String[]> forms =
          Map.of(
              "csv", new String[] {hba, redacted},
              "json",
                  new String[] {
                    Json.newObject().put(HBA, hba).put("other", "keep").toString(),
                    Json.newObject().put(HBA, redacted).put("other", "keep").toString()
                  },
              "server.conf",
                  new String[] {
                    "--" + HBA + "=" + hba + "\n--placement_cloud=gcp\n",
                    "--" + HBA + "=" + redacted + "\n--placement_cloud=gcp\n"
                  },
              "map",
                  new String[] {
                    "{" + HBA + "=" + hba + ", max_log_size=256}",
                    "{" + HBA + "=" + redacted + ", max_log_size=256}"
                  });
      for (Map.Entry<String, String[]> form : forms.entrySet()) {
        String in = form.getValue()[0];
        String expected = form.getValue()[1];
        check(
            failures,
            form.getKey() + " redactAllValues",
            in,
            LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT),
            expected);
        check(
            failures,
            form.getKey() + " redactSensitiveInfoInString",
            in,
            RedactingService.redactSensitiveInfoInString(in),
            expected);
      }
      ObjectNode details = Json.newObject();
      details.putObject("tserverGFlags").put(HBA, hba);
      check(
          failures,
          "filterSecretFields APIS",
          hba,
          RedactingService.filterSecretFields(details, RedactionTarget.APIS, null, null, true)
              .get("tserverGFlags")
              .get(HBA)
              .asText(),
          redacted);
      String restored;
      try {
        restored =
            GFlagsUtil.mergeSensitiveGFlags(Map.of(HBA, hba), Map.of(HBA, redacted), null, null)
                .get(HBA);
      } catch (PlatformServiceException e) {
        restored = e.getMessage();
      }
      check(failures, "mergeSensitiveGFlags", redacted, restored, hba);
    }
    assertTrue(String.join("\n", failures), failures.isEmpty());
  }
}

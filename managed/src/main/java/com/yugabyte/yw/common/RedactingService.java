// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.PathNotFoundException;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Universe;
import java.util.Base64;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArraySet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class RedactingService {

  private final GFlagsValidation gFlagsValidation;

  @Inject
  public RedactingService(GFlagsValidation gFlagsValidation) {
    this.gFlagsValidation = gFlagsValidation;
  }

  public static final String SECRET_REPLACEMENT = "REDACTED";
  public static final List<String> SECRET_PATHS_FOR_APIS =
      ImmutableList.of(
          "$..ysqlPassword",
          "$..ycqlPassword",
          "$..ysqlAdminPassword",
          "$..ycqlAdminPassword",
          "$..ysqlCurrAdminPassword",
          "$..ycqlCurrAdminPassword",
          "$..ycqlNewPassword",
          "$..ysqlNewPassword",
          "$..ycqlCurrentPassword",
          "$..ysqlCurrentPassword",
          "$..sshPrivateKeyContent");

  private static final String YCQL_LDAP_BIND_PASSWD_PATH = "$..ycql_ldap_bind_passwd";

  public static final List<String> SECRET_QUERY_PARAMS_FOR_LOGS =
      ImmutableList.of(/* SAS Token */ "sig");

  public static final List<String> SECRET_PATHS_FOR_LOGS =
      ImmutableList.<String>builder()
          .addAll(SECRET_PATHS_FOR_APIS)
          .add(YCQL_LDAP_BIND_PASSWD_PATH)
          .add("$..password")
          .add("$..confirmPassword")
          .add("$..newPassword")
          .add("$..currentPassword")
          .add("$..['config.AWS_ACCESS_KEY_ID']")
          .add("$..['config.AWS_SECRET_ACCESS_KEY']")
          .add("$..config.accessKey")
          .add("$..config.secretKey")
          // Datadog API key
          .add("$..config.apiKey")
          // GCP private key
          .add("$..['config.config_file_contents.private_key_id']")
          .add("$..['config.config_file_contents.private_key']")
          .add("$..config_file_contents.private_key")
          .add("$..config_file_contents.private_key_id")
          .add("$..config.private_key_id")
          .add("$..config.private_key")
          .add("$..credentials.private_key_id")
          .add("$..credentials.private_key")
          .add("$..credentialsString")
          .add("$..GCP_CONFIG.private_key_id")
          .add("$..GCP_CONFIG.private_key")
          .add("$..gceApplicationCredentialsPath")
          .add("$..gceApplicationCredentials")
          // Azure client secret
          .add("$..AZURE_CLIENT_SECRET")
          .add("$..CLIENT_SECRET")
          .add("$..azuClientSecret")
          // OCI private key and fingerprint
          .add("$..ociPrivateKeyContent")
          .add("$..OCI_PRIVATE_KEY_CONTENT")
          .add("$..ociFingerprint")
          .add("$..OCI_FINGERPRINT")
          // Kubernetes secrets
          .add("$..KUBECONFIG_PULL_SECRET_CONTENT")
          .add("$..KUBECONFIG_CONTENT")
          .add("$..kubeConfigContent")
          .add("$..kubernetesPullSecretContent")
          // onprem and certificate private keys
          .add("$..keyContent")
          .add("$..certContent")
          .add("$..customServerCertData")
          .add("$..['customServerCertData.serverKeyContent']")
          // S3 storage credentials
          .add("$..AWS_ACCESS_KEY_ID")
          .add("$..AWS_SECRET_ACCESS_KEY")
          .add("$..awsAccessKeyID")
          .add("$..awsAccessKeySecret")
          // GCS storage credentials
          .add("$..GCS_CREDENTIALS_JSON")
          // Azure storage credentials
          .add("$..AZURE_STORAGE_SAS_TOKEN")
          // OCI S3-compatible storage credentials
          .add("$..OCI_S3_ACCESS_KEY_ID")
          .add("$..OCI_S3_SECRET_ACCESS_KEY")
          // SmartKey API key
          .add("$..api_key")
          // SMTP password
          .add("$..smtpPassword")
          // Hashicorp token
          .add("$..HC_VAULT_TOKEN")
          .add("$..vaultToken")
          // Hashicorp RoleID
          .add("$..HC_VAULT_ROLE_ID")
          .add("$..vaultRoleID")
          // Hashicorp SecretID
          .add("$..HC_VAULT_SECRET_ID")
          .add("$..vaultSecretID")
          .add("$..token")
          // Custom hooks
          .add("$..hook.hookText")
          .add("$..hook.runtimeArgs")
          // LDAP - DB Universe Sync
          .add("$..dbuserPassword")
          .add("$..ldapBindPassword")
          // HA Config
          .add("$..cluster_key")
          // CipherTrust creds
          .add("$..REFRESH_TOKEN")
          .add("$..PASSWORD")
          // Dynatrace API token
          .add("$..apiToken")
          .build();

  // List of json paths to any secret fields we want to redact.
  // More on json path format can be found here: https://goessner.net/articles/JsonPath/
  public static final List<JsonPath> SECRET_JSON_PATHS_APIS =
      SECRET_PATHS_FOR_APIS.stream().map(JsonPath::compile).collect(Collectors.toList());

  private static final JsonPath YCQL_LDAP_BIND_PASSWD_JSON_PATH =
      JsonPath.compile(YCQL_LDAP_BIND_PASSWD_PATH);

  public static Set<JsonPath> SECRET_JSON_PATHS_LOGS =
      new CopyOnWriteArraySet<>(
          SECRET_PATHS_FOR_LOGS.stream().map(JsonPath::compile).collect(Collectors.toList()));

  /** Returns a safe fallback ObjectNode when JSON reparsing after redaction fails. */
  private static ObjectNode redactionParseErrorFallback(String redactedPayload) {
    ObjectNode fallback = Json.mapper().createObjectNode();
    fallback.put("_redactionParseError", true);
    fallback.put("_redactedPayload", redactedPayload);
    return fallback;
  }

  // Used to redact root CA keys in the helm values
  // Regex pattern to match "<Any_String>key: <Base64_encoded_string>"
  private static final String KEY_REGEX = "(.*key):\\s+(\\S+)";
  private static final Pattern KEY_SEARCH_PATTERN = Pattern.compile(KEY_REGEX);

  public static JsonNode filterSecretFields(JsonNode input, RedactionTarget target) {
    return filterSecretFields(input, target, null, null);
  }

  public static JsonNode filterSecretFields(
      JsonNode input,
      RedactionTarget target,
      String ybSoftwareVersion,
      GFlagsValidation gFlagsValidation) {
    return filterSecretFields(
        input,
        target,
        ybSoftwareVersion,
        gFlagsValidation,
        isGFlagsSensitiveDataApiRedactionEnabled());
  }

  public static boolean isGFlagsSensitiveDataApiRedactionEnabled() {
    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
    Boolean enabled =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction);
    return !Boolean.FALSE.equals(enabled);
  }

  public static JsonNode filterSecretFields(
      JsonNode input,
      RedactionTarget target,
      String ybSoftwareVersion,
      GFlagsValidation gFlagsValidation,
      boolean enableGFlagsSensitiveDataApiRedaction) {
    if (input == null) {
      return null;
    }

    DocumentContext context = JsonPath.parse(input.deepCopy(), AuditService.JSONPATH_CONFIG);

    switch (target) {
      case APIS:
        SECRET_JSON_PATHS_APIS.forEach(path -> redactJsonPath(context, path));
        if (enableGFlagsSensitiveDataApiRedaction) {
          redactJsonPath(context, YCQL_LDAP_BIND_PASSWD_JSON_PATH);
        }
        break;
      case LOGS:
        SECRET_JSON_PATHS_LOGS.forEach(
            path -> {
              try {
                // ysql_hba_conf_csv is redacted field-by-field (ldapbindpasswd only), not
                // wholesale.
                if (path.getPath().contains("ysql_hba_conf_csv")) {
                  return;
                }
                context.set(path, SECRET_REPLACEMENT);
              } catch (PathNotFoundException e) {
                log.trace("skip redacting secret path {} - not present", path.getPath());
              }
            });
        break;
      default:
        throw new IllegalArgumentException("Target " + target.name() + " is not supported");
    }

    JsonNode afterJsonPath = context.json();

    if (target == RedactionTarget.LOGS) {
      return redactSensitiveGFlagFields(afterJsonPath, ybSoftwareVersion, gFlagsValidation);
    }

    if (target == RedactionTarget.APIS) {
      if (enableGFlagsSensitiveDataApiRedaction) {
        try {
          afterJsonPath = redactAuditAdditionalDetails(afterJsonPath, null, gFlagsValidation);
          afterJsonPath = redactLdapBindPasswdInYsqlHbaConf(afterJsonPath);
        } catch (Exception e) {
          log.warn("Gflags sensitive data API redaction failed; skipping it.", e);
        }
      }
    }

    return afterJsonPath;
  }

  private static void redactJsonPath(DocumentContext context, JsonPath path) {
    try {
      context.set(path, SECRET_REPLACEMENT);
    } catch (PathNotFoundException e) {
      log.trace("skip redacting secret path {} - not present", path.getPath());
    }
  }

  public static JsonNode redactSensitiveGFlagFields(JsonNode input) {
    return redactSensitiveGFlagFields(input, null, null);
  }

  public static JsonNode redactSensitiveGFlagFields(
      JsonNode input, String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    final JsonNode original = input;
    try {
      if (input == null || input.isMissingNode() || input.isNull()) {
        return input;
      }
      input = redactAuditAdditionalDetails(input, null, gFlagsValidation);
      String jsonString = input.toString();
      String redactedString =
          redactSensitiveInfoInString(jsonString, ybSoftwareVersion, gFlagsValidation);

      try {
        return Json.parse(redactedString);
      } catch (Exception parseException) {
        log.warn(
            "Failed to parse JSON after sensitive gflag redaction; returning fallback wrapper"
                + " with string-redacted payload. Error: ",
            parseException);
        return redactionParseErrorFallback(redactedString);
      }

    } catch (Exception e) {
      log.warn(
          "Failed to apply sensitive gflag redaction; returning string-redacted fallback. Error: ",
          e);
      if (original == null || original.isNull() || original.isMissingNode()) {
        return original;
      }
      try {
        String redactedString =
            redactSensitiveInfoInString(original.toString(), ybSoftwareVersion, gFlagsValidation);
        return redactionParseErrorFallback(redactedString);
      } catch (Exception e2) {
        log.warn("String redaction after sensitive gflag redaction failure also failed: ", e2);
        return redactionParseErrorFallback(SECRET_REPLACEMENT);
      }
    }
  }

  /** Redacts all sensitive field values (LDAP passwords, sensitive gflags) found in a string. */
  public static String redactSensitiveInfoInString(String input) {
    return redactSensitiveInfoInString(input, null, null);
  }

  public static String redactSensitiveInfoInString(
      String input, String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    String output = input;
    if (input == null) {
      return output;
    }

    Set<String> allPatternsToRedact = new HashSet<>();
    allPatternsToRedact.add("ldapbindpasswd");
    allPatternsToRedact.add("ldapBindPassword");
    allPatternsToRedact.add("ycql_ldap_bind_passwd");
    allPatternsToRedact.add("ldapServiceAccountPassword");

    if (gFlagsValidation != null) {
      try {
        if (ybSoftwareVersion != null) {
          Set<String> sensitiveGflags =
              getSensitiveGflagsForRedaction(ybSoftwareVersion, gFlagsValidation);
          // ysql_hba_conf_csv is redacted field-by-field (ldapbindpasswd only), not wholesale.
          Set<String> filteredGflags =
              sensitiveGflags.stream()
                  .filter(gflag -> !gflag.equals("ysql_hba_conf_csv"))
                  .collect(Collectors.toSet());
          allPatternsToRedact.addAll(filteredGflags);
        }
      } catch (Exception e) {
        log.warn("Exception in sensitive gflags processing: ", e);
      }
    }
    for (String pattern : allPatternsToRedact) {
      output = redactPatternOccurrences(pattern, output);
    }

    return output;
  }

  /**
   * Routes a single field name through all applicable string-scan redaction helpers. ldapbindpasswd
   * uses the HBA parser first (to preserve quoting), then only JSON-key fallbacks. All other
   * patterns go through the full set of JSON, assignment, CLI-flag, and Python-dict redactors.
   */
  private static String redactPatternOccurrences(String pattern, String output) {
    if ("ldapbindpasswd".equals(pattern)) {
      output = redactAllLdapBindPasswdHbaFormattedValues(output);
      if (!output.contains(pattern)) {
        return output;
      }
      // HBA parser handles all HBA quoting; only JSON-key forms ("ldapbindpasswd": ...) remain.
      output = redactStandardJsonQuotedStringValue(output, pattern, SECRET_REPLACEMENT);
      output = redactBackslashEscapedJsonQuotedKeyValue(output, pattern, SECRET_REPLACEMENT);
      output = redactJsonBareValue(output, pattern, SECRET_REPLACEMENT);
      return output;
    }
    if (!output.contains(pattern)) {
      return output;
    }
    output = redactStandardJsonQuotedStringValue(output, pattern, SECRET_REPLACEMENT);
    output = redactJsonBareValue(output, pattern, SECRET_REPLACEMENT);
    output = redactBackslashEscapedJsonQuotedKeyValue(output, pattern, SECRET_REPLACEMENT);
    output = redactKeyEqualsValue(output, pattern, SECRET_REPLACEMENT);
    output = redactCliFlagValue(output, pattern, SECRET_REPLACEMENT);
    output = redactPythonDictValue(output, pattern, SECRET_REPLACEMENT);
    return output;
  }

  /** Linear scan for "field": "value" redaction; avoids stack-heavy regex on huge strings. */
  private static String redactStandardJsonQuotedStringValue(
      String input, String field, String replacement) {
    String keyQuoted = "\"" + field + "\"";
    int from = 0;
    StringBuilder out = new StringBuilder(input.length());
    while (from < input.length()) {
      int keyIdx = input.indexOf(keyQuoted, from);
      if (keyIdx < 0) {
        out.append(input, from, input.length());
        break;
      }
      // Substrings like \"ycql_ldap_bind_passwd\" inside a larger JSON string are not object keys;
      // treating them as keys drops the real closing quote and breaks Json.parse.
      if (!isStructuralJsonObjectKeyQuote(input, keyIdx)) {
        out.append(input, from, keyIdx + 1);
        from = keyIdx + 1;
        continue;
      }
      out.append(input, from, keyIdx);
      int p = keyIdx + keyQuoted.length();
      while (p < input.length() && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p >= input.length() || input.charAt(p) != ':') {
        out.append(input.charAt(keyIdx));
        from = keyIdx + 1;
        continue;
      }
      p++;
      while (p < input.length() && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p >= input.length() || input.charAt(p) != '"') {
        int next = keyIdx + keyQuoted.length();
        out.append(input, keyIdx, next);
        from = next;
        continue;
      }
      int valueContentStart = p + 1;
      int closingQuote = indexOfUnescapedDoubleQuote(input, valueContentStart);
      if (closingQuote < 0) {
        out.append(input, keyIdx, input.length());
        break;
      }
      out.append(input, keyIdx, p + 1);
      out.append(replacement);
      out.append('"');
      from = closingQuote + 1;
    }
    return out.toString();
  }

  /**
   * True if this double-quote starts an object key in raw JSON, not an escaped quote inside a
   * string value.
   */
  private static boolean isStructuralJsonObjectKeyQuote(String s, int quoteIdx) {
    if (quoteIdx < 0 || quoteIdx >= s.length() || s.charAt(quoteIdx) != '"') {
      return false;
    }
    if (isBackslashEscapedDoubleQuote(s, quoteIdx)) {
      return false;
    }
    int i = quoteIdx - 1;
    while (i >= 0 && Character.isWhitespace(s.charAt(i))) {
      i--;
    }
    if (i < 0) {
      return true;
    }
    char c = s.charAt(i);
    return c == '{' || c == ',' || c == '[';
  }

  /** Odd number of backslashes before " means the quote is escaped inside a JSON string. */
  private static boolean isBackslashEscapedDoubleQuote(String s, int quoteIdx) {
    int backslashes = 0;
    for (int i = quoteIdx - 1; i >= 0 && s.charAt(i) == '\\'; i--) {
      backslashes++;
    }
    return (backslashes & 1) == 1;
  }

  /** Redacts backslash-escaped JSON key/value pairs in host strings and logs; linear scan. */
  private static String redactBackslashEscapedJsonQuotedKeyValue(
      String input, String field, String replacement) {
    String escapedKey = "\\\"" + field + "\\\"";
    int from = 0;
    StringBuilder out = new StringBuilder(input.length());
    while (from < input.length()) {
      int keyIdx = input.indexOf(escapedKey, from);
      if (keyIdx < 0) {
        out.append(input, from, input.length());
        break;
      }
      out.append(input, from, keyIdx);
      int p = keyIdx + escapedKey.length();
      while (p < input.length() && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p >= input.length() || input.charAt(p) != ':') {
        out.append(input.charAt(keyIdx));
        from = keyIdx + 1;
        continue;
      }
      p++;
      while (p < input.length() && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p + 1 >= input.length() || input.charAt(p) != '\\' || input.charAt(p + 1) != '"') {
        out.append(input.charAt(keyIdx));
        from = keyIdx + 1;
        continue;
      }
      int valueContentStart = p + 2;
      int closingBackslash =
          indexOfClosingBackslashQuoteInEscapedJsonString(input, valueContentStart);
      if (closingBackslash < 0) {
        out.append(input, keyIdx, input.length());
        break;
      }
      out.append(input, keyIdx, valueContentStart);
      out.append(replacement);
      from = closingBackslash + 2;
    }
    return out.toString();
  }

  /** Closing backslash-quote index for an escaped JSON string value starting at start. */
  private static int indexOfClosingBackslashQuoteInEscapedJsonString(String s, int start) {
    int i = start;
    int n = s.length();
    while (i < n) {
      if (s.charAt(i) == '\\' && i + 1 < n && s.charAt(i + 1) == '"') {
        int after = i + 2;
        while (after < n && Character.isWhitespace(s.charAt(after))) {
          after++;
        }
        if (after >= n) {
          return i;
        }
        char c = s.charAt(after);
        if (c == '}' || c == ',' || c == ']' || c == '"') {
          return i;
        }
        i += 2;
        continue;
      }
      i++;
    }
    return -1;
  }

  private static int indexOfUnescapedDoubleQuote(String s, int start) {
    int i = start;
    while (i < s.length()) {
      char c = s.charAt(i);
      if (c == '"') {
        return i;
      }
      if (c == '\\' && i + 1 < s.length()) {
        i += 2;
        continue;
      }
      i++;
    }
    return -1;
  }

  private static boolean isFlagNameStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
  }

  private static boolean isFlagNameChar(char c) {
    return isFlagNameStart(c) || (c >= '0' && c <= '9');
  }

  /** Redacts "field": bareValue where the value is not a JSON string (e.g. a bare number). */
  private static String redactJsonBareValue(String input, String field, String replacement) {
    String keyQuoted = "\"" + field + "\"";
    if (!input.contains(keyQuoted)) {
      return input;
    }
    int n = input.length();
    int from = 0;
    StringBuilder out = new StringBuilder(n);
    while (true) {
      int keyIdx = input.indexOf(keyQuoted, from);
      if (keyIdx < 0) {
        out.append(input, from, n);
        break;
      }
      if (!isStructuralJsonObjectKeyQuote(input, keyIdx)) {
        out.append(input, from, keyIdx + 1);
        from = keyIdx + 1;
        continue;
      }
      int p = keyIdx + keyQuoted.length();
      while (p < n && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p >= n || input.charAt(p) != ':') {
        out.append(input, from, keyIdx + keyQuoted.length());
        from = keyIdx + keyQuoted.length();
        continue;
      }
      p++;
      while (p < n && Character.isWhitespace(input.charAt(p))) {
        p++;
      }
      if (p >= n || input.charAt(p) == '"') {
        // Quoted value — handled by redactStandardJsonQuotedStringValue; skip here.
        out.append(input, from, keyIdx + keyQuoted.length());
        from = keyIdx + keyQuoted.length();
        continue;
      }
      char vc = input.charAt(p);
      if (vc == ',' || vc == '}' || vc == ']') {
        // Empty / structural delimiter — nothing to redact.
        out.append(input, from, keyIdx + keyQuoted.length());
        from = keyIdx + keyQuoted.length();
        continue;
      }
      // Non-quoted value: consume until a JSON structural delimiter or whitespace.
      int vend = p;
      while (vend < n
          && input.charAt(vend) != ','
          && input.charAt(vend) != '}'
          && input.charAt(vend) != ']'
          && !Character.isWhitespace(input.charAt(vend))) {
        vend++;
      }
      out.append(input, from, p);
      out.append(replacement);
      from = vend;
    }
    return out.toString();
  }

  /**
   * Redacts field=value wherever it appears. Handles unquoted values, CSV-quoted ("value"), and
   * escaped-quoted (\"value\") forms. Unquoted values end at a newline, backslash, lone quote, or
   * comma followed by the next key name.
   */
  private static String redactKeyEqualsValue(String input, String field, String replacement) {
    String needle = field + "=";
    int pos = input.indexOf(needle);
    if (pos < 0) {
      return input;
    }
    StringBuilder out = new StringBuilder(input.length());
    int from = 0;
    while (pos >= 0) {
      out.append(input, from, pos + needle.length());
      int vs = pos + needle.length();
      if (vs >= input.length()) {
        from = vs;
        break;
      }
      char c = input.charAt(vs);
      int ve;
      if (c == '"') {
        // CSV-quoted value: "value" (handles "" as escaped ")
        ve = scanPastCsvQuotedValue(input, vs + 1);
        out.append('"').append(replacement).append('"');
      } else if (c == '\\' && vs + 1 < input.length() && input.charAt(vs + 1) == '"') {
        // Escaped-quoted value: \"value\" (handles \"\" as escaped ")
        ve = scanPastEscapedCsvQuotedValue(input, vs + 2);
        out.append("\\\"").append(replacement).append("\\\"");
      } else {
        // Unquoted value
        ve = findUnquotedValueEnd(input, vs);
        out.append(replacement);
      }
      from = ve;
      pos = input.indexOf(needle, from);
    }
    out.append(input, from, input.length());
    return out.toString();
  }

  /** Returns the index after the closing " of a CSV-quoted value; treats "" as a literal ". */
  private static int scanPastCsvQuotedValue(String s, int start) {
    for (int i = start, n = s.length(); i < n; ) {
      if (s.charAt(i) == '"') {
        if (i + 1 < n && s.charAt(i + 1) == '"') {
          i += 2; // "" is a literal "
        } else {
          return i + 1;
        }
      } else {
        i++;
      }
    }
    return s.length();
  }

  /**
   * Returns the index after the closing \" of an escaped-CSV-quoted value; treats \"\" as a literal
   * ".
   */
  private static int scanPastEscapedCsvQuotedValue(String s, int start) {
    for (int i = start, n = s.length(); i < n; ) {
      if (s.charAt(i) == '\\' && i + 1 < n && s.charAt(i + 1) == '"') {
        if (i + 2 < n && s.charAt(i + 2) == '\\' && i + 3 < n && s.charAt(i + 3) == '"') {
          i += 4; // \"\" is a literal "
        } else {
          return i + 2;
        }
      } else {
        i++;
      }
    }
    return s.length();
  }

  /**
   * Returns the end of an unquoted value after key=. Stops at a newline or lone " followed by a
   * JSON structural delimiter. A doubled "" is treated as an embedded literal quote and skipped. A
   * backslash is treated as a literal character (not a stop) so passwords like pass\word are fully
   * consumed; the " check handles JSON-embedded context without needing the backslash stop. A comma
   * followed by an identifier of 3+ chars and "=" signals the next key (conf file or Combined
   * gflags format). A comma followed by "--" signals the next CLI flag (command arrays). The 3+
   * char minimum for identifiers prevents DN components (OU=, DC=, CN=) from being mistaken for key
   * boundaries when redacting fields like ycql_ldap_bind_dn.
   */
  private static int findUnquotedValueEnd(String s, int start) {
    int n = s.length();
    for (int i = start; i < n; i++) {
      char c = s.charAt(i);
      if (c == '\n' || c == '\r') {
        return i;
      }
      if (c == '"') {
        if (i + 1 < n && s.charAt(i + 1) == '"') {
          i++; // "" is an embedded quote — skip both and continue
          continue;
        }
        // Only treat " as a JSON string boundary when followed by a JSON structural delimiter.
        // A " inside a conf-file or log-line password (e.g. h*Y"abc"P) is a literal character.
        char next = i + 1 < n ? s.charAt(i + 1) : 0;
        if (next == '}' || next == ',' || next == ']' || next == ':' || next == 0) {
          return i;
        }
        if (Character.isWhitespace(next)) {
          int j = i + 2;
          while (j < n && Character.isWhitespace(s.charAt(j))) j++;
          char lookahead = j < n ? s.charAt(j) : 0;
          if (lookahead == '}'
              || lookahead == ','
              || lookahead == ']'
              || lookahead == ':'
              || lookahead == 0) {
            return i;
          }
        }
        continue; // literal " in the value
      }
      if (c == ',') {
        int j = i + 1;
        while (j < n && Character.isWhitespace(s.charAt(j))) {
          j++;
        }
        // Stop at ", identifier=" where the identifier is 3+ chars (conf file / Combined gflags).
        // The 3+ minimum avoids stopping at 2-char DN components like OU=, DC=, CN=.
        if (j < n && isFlagNameStart(s.charAt(j))) {
          int k = j + 1;
          while (k < n && isFlagNameChar(s.charAt(k))) {
            k++;
          }
          if (k - j >= 3 && k < n && s.charAt(k) == '=') {
            return i;
          }
        }
        // Stop at ", --" — CLI flag array boundary (e.g. --version command logging).
        if (j + 1 < n && s.charAt(j) == '-' && s.charAt(j + 1) == '-') {
          return i;
        }
      }
    }
    return n;
  }

  /**
   * Redacts --field value in all CLI quoting styles: plain, single-quoted, and
   * escaped-double-quoted.
   */
  private static String redactCliFlagValue(String input, String field, String replacement) {
    if (!input.contains("--" + field)) {
      return input;
    }
    input = redactCliFlagPlain(input, field, replacement);
    input = redactCliFlagSingleQuoted(input, field, replacement);
    input = redactCliFlagEscDblQuoted(input, field, replacement);
    return input;
  }

  /**
   * Redacts plain (unquoted) CLI flags: --field, value or --field value. Stops at the next " --" or
   * ", --". Skips occurrences inside quoted contexts.
   */
  private static String redactCliFlagPlain(String input, String field, String replacement) {
    String token = "--" + field;
    int n = input.length();
    int from = 0;
    StringBuilder out = new StringBuilder(n);
    while (true) {
      int idx = input.indexOf(token, from);
      if (idx < 0) {
        out.append(input, from, n);
        break;
      }
      // Skip occurrences inside quoted contexts (handled by single/esc-dbl variants).
      char before = idx > 0 ? input.charAt(idx - 1) : 0;
      if (before == '\'' || (before == '"' && idx >= 2 && input.charAt(idx - 2) == '\\')) {
        out.append(input, from, idx + 1);
        from = idx + 1;
        continue;
      }
      int p = idx + token.length();
      // Consume optional comma then spaces (separator between flag name and value).
      boolean hasSep = false;
      if (p < n && input.charAt(p) == ',') {
        p++;
        hasSep = true;
      }
      while (p < n && input.charAt(p) == ' ') {
        p++;
        hasSep = true;
      }
      if (!hasSep || p >= n) {
        // No value separator — not a flag=value occurrence; copy flag token and move on.
        out.append(input, from, idx + token.length());
        from = idx + token.length();
        continue;
      }
      int valueEnd = findCliFlagValueEnd(input, p);
      out.append(input, from, idx + token.length());
      out.append(", ").append(replacement);
      from = valueEnd;
    }
    return out.toString();
  }

  /** Returns the start of the next CLI flag separator (" --" or ", --"), or end of string. */
  private static int findCliFlagValueEnd(String s, int start) {
    int n = s.length();
    for (int i = start; i < n; i++) {
      char c = s.charAt(i);
      // " --" or "\n--"
      if ((c == ' ' || c == '\n')
          && i + 2 < n
          && s.charAt(i + 1) == '-'
          && s.charAt(i + 2) == '-') {
        return i;
      }
      // ", --"
      if (c == ',') {
        int j = i + 1;
        while (j < n && s.charAt(j) == ' ') {
          j++;
        }
        if (j + 1 < n && s.charAt(j) == '-' && s.charAt(j + 1) == '-') {
          return i;
        }
      }
    }
    return n;
  }

  /** Redacts single-quoted CLI flags: '--field', 'value'. */
  private static String redactCliFlagSingleQuoted(String input, String field, String replacement) {
    String prefix = "'" + "--" + field + "',";
    if (!input.contains(prefix)) {
      return input;
    }
    int n = input.length();
    int from = 0;
    StringBuilder out = new StringBuilder(n);
    while (true) {
      int idx = input.indexOf(prefix, from);
      if (idx < 0) {
        out.append(input, from, n);
        break;
      }
      out.append(input, from, idx + prefix.length());
      int p = idx + prefix.length();
      while (p < n && input.charAt(p) == ' ') {
        p++;
      }
      if (p < n && input.charAt(p) == '\'') {
        // Skip opening ', find and skip closing '
        int valueEnd = input.indexOf('\'', p + 1);
        if (valueEnd < 0) {
          valueEnd = n - 1;
        }
        out.append(' ').append(replacement);
        from = valueEnd + 1;
      } else {
        // No surrounding quotes — consume until comma or end
        int valueEnd = p;
        while (valueEnd < n && input.charAt(valueEnd) != ',' && input.charAt(valueEnd) != '\'') {
          valueEnd++;
        }
        out.append(' ').append(replacement);
        from = valueEnd;
      }
    }
    return out.toString();
  }

  /** Redacts escaped-double-quoted CLI flags: \"--field\", \"value\". */
  private static String redactCliFlagEscDblQuoted(String input, String field, String replacement) {
    String prefix = "\\\"" + "--" + field + "\\\",";
    if (!input.contains(prefix)) {
      return input;
    }
    int n = input.length();
    int from = 0;
    StringBuilder out = new StringBuilder(n);
    while (true) {
      int idx = input.indexOf(prefix, from);
      if (idx < 0) {
        out.append(input, from, n);
        break;
      }
      out.append(input, from, idx + prefix.length());
      int p = idx + prefix.length();
      while (p < n && input.charAt(p) == ' ') {
        p++;
      }
      if (p + 1 < n && input.charAt(p) == '\\' && input.charAt(p + 1) == '"') {
        // Skip opening \", find closing \"
        int vs = p + 2;
        int ve = vs;
        while (ve + 1 < n && !(input.charAt(ve) == '\\' && input.charAt(ve + 1) == '"')) {
          ve++;
        }
        out.append(' ').append(replacement);
        from = ve + 2;
      } else {
        int valueEnd = p;
        while (valueEnd < n && input.charAt(valueEnd) != ',' && input.charAt(valueEnd) != ' ') {
          valueEnd++;
        }
        out.append(' ').append(replacement);
        from = valueEnd;
      }
    }
    return out.toString();
  }

  /** Redacts the value in Python/repr dict entries: {'key': 'field', 'value': 'secret'}. */
  private static String redactPythonDictValue(String input, String field, String replacement) {
    String keyMarker = "'key': '" + field + "'";
    if (!input.contains(keyMarker)) {
      return input;
    }
    int n = input.length();
    int from = 0;
    StringBuilder out = new StringBuilder(n);
    while (true) {
      int idx = input.indexOf(keyMarker, from);
      if (idx < 0) {
        out.append(input, from, n);
        break;
      }
      int p = idx + keyMarker.length();
      // Skip optional whitespace then comma
      while (p < n && (input.charAt(p) == ' ' || input.charAt(p) == '\t')) {
        p++;
      }
      if (p >= n || input.charAt(p) != ',') {
        out.append(input, from, idx + 1);
        from = idx + 1;
        continue;
      }
      p++; // skip comma
      while (p < n && (input.charAt(p) == ' ' || input.charAt(p) == '\t')) {
        p++;
      }
      // Expect 'value': '
      String valueMarker = "'value': '";
      if (!input.startsWith(valueMarker, p)) {
        out.append(input, from, idx + 1);
        from = idx + 1;
        continue;
      }
      p += valueMarker.length();
      // Find closing '
      int valueEnd = input.indexOf('\'', p);
      if (valueEnd < 0) {
        valueEnd = n;
      }
      out.append(input, from, p);
      out.append(replacement).append('\'');
      from = valueEnd + 1;
    }
    return out.toString();
  }

  /** Redacts sensitive gflags in the additionalDetails section of audit entries. */
  public static JsonNode redactAuditAdditionalDetails(
      JsonNode jsonData, Universe universe, GFlagsValidation gFlagsValidation) {
    try {
      if (jsonData == null) {
        return jsonData;
      }
      if (jsonData.isArray()) {
        for (JsonNode auditEntry : jsonData) {
          redactSingleAuditEntry(auditEntry, universe, gFlagsValidation);
        }
      } else if (jsonData.isObject()) {
        redactSingleAuditEntry(jsonData, universe, gFlagsValidation);
      }
    } catch (Exception e) {
      log.warn("Error while redacting audit additionalDetails: ", e);
    }
    return jsonData;
  }

  private static void redactSingleAuditEntry(
      JsonNode auditEntry, Universe universe, GFlagsValidation gFlagsValidation) {
    if (auditEntry == null || !auditEntry.isObject()) {
      return;
    }
    if (auditEntry.has("additionalDetails")) {
      JsonNode additionalDetails = auditEntry.get("additionalDetails");
      if (additionalDetails != null && !additionalDetails.isNull()) {
        if (additionalDetails.has("gflags")) {
          redactGflagsSection(additionalDetails.get("gflags"), universe, gFlagsValidation);
        }
        if (additionalDetails.has("readonly_cluster_gflags")) {
          redactGflagsSection(
              additionalDetails.get("readonly_cluster_gflags"), universe, gFlagsValidation);
        }
      }
    }
  }

  /** Redacts all ldapbindpasswd values in HBA-formatted strings, preserving quoting style. */
  public static String redactAllLdapBindPasswdHbaFormattedValues(String input) {
    return LdapBindPasswdHbaFormat.redactAllValues(input, SECRET_REPLACEMENT);
  }

  /** Redacts ldapbindpasswd within ysql_hba_conf_csv fields in API response JSON. */
  private static JsonNode redactLdapBindPasswdInYsqlHbaConf(JsonNode input) {
    if (input == null) {
      return input;
    }
    try {
      redactLdapBindPasswdRecursive(input);
      return input;
    } catch (Exception e) {
      log.warn("Error redacting ldapbindpasswd in ysql_hba_conf_csv: ", e);
      return input;
    }
  }

  private static void redactLdapBindPasswdRecursive(JsonNode node) {
    if (node == null) {
      return;
    }
    if (node.isObject()) {
      ObjectNode objNode = (ObjectNode) node;
      node.fieldNames()
          .forEachRemaining(
              fieldName -> {
                JsonNode fieldValue = objNode.get(fieldName);
                if (fieldValue != null && fieldValue.isTextual()) {
                  if (fieldName.equals("ysql_hba_conf_csv")) {
                    String originalValue = fieldValue.asText();
                    String redactedValue = redactAllLdapBindPasswdHbaFormattedValues(originalValue);
                    objNode.put(fieldName, redactedValue);
                  }
                } else if (fieldValue != null && (fieldValue.isObject() || fieldValue.isArray())) {
                  redactLdapBindPasswdRecursive(fieldValue);
                }
              });
    } else if (node.isArray()) {
      for (JsonNode element : node) {
        redactLdapBindPasswdRecursive(element);
      }
    }
  }

  public static void redactGflagsSection(JsonNode gflags) {
    redactGflagsSection(gflags, null, null);
  }

  /** Redacts sensitive gflags in the gflags section for both master and tserver process types. */
  public static void redactGflagsSection(
      JsonNode gflags, Universe universe, GFlagsValidation gFlagsValidation) {
    try {
      if (gflags == null) {
        return;
      }
      String ybSoftwareVersion = getYbSoftwareVersion(universe);
      Set<String> sensitiveGflags =
          getSensitiveGflagsForRedaction(ybSoftwareVersion, gFlagsValidation);

      for (String gflagType : new String[] {"tserver", "master"}) {
        if (gflags.has(gflagType) && gflags.get(gflagType).isArray()) {
          for (JsonNode flag : gflags.get(gflagType)) {
            if (flag != null && flag.has("name")) {
              String flagName = flag.get("name").asText();
              // Since we had excluded ysql_hba_conf_csv from sensitive gflags, manually added in
              // condition.
              if (sensitiveGflags.contains(flagName)
                  || flagName.equals(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
                ObjectNode flagNode = (ObjectNode) flag;
                for (String field : new String[] {"old", "new", "default"}) {
                  if (flag.has(field)) {
                    JsonNode fieldValue = flag.get(field);
                    if (!fieldValue.isNull()
                        && !(fieldValue.isTextual() && fieldValue.asText().isEmpty())) {
                      if (flagName.equals(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
                        // ysql_hba_conf_csv is redacted field-by-field (ldapbindpasswd only).
                        flagNode.put(
                            field,
                            redactSensitiveInfoInString(
                                fieldValue.asText(), ybSoftwareVersion, gFlagsValidation));
                      } else {
                        flagNode.put(field, SECRET_REPLACEMENT);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    } catch (Exception e) {
      log.warn("Error while redacting gflags section: ", e);
    }
  }

  public static Set<String> getSensitiveGflagsForRedaction(
      String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    Set<String> allSensitiveGflags = new HashSet<>();

    try {
      Set<String> jsonPaths = getSensitiveJsonPathsForVersion(ybSoftwareVersion, gFlagsValidation);
      if (jsonPaths != null) {
        for (String jsonPath : jsonPaths) {
          if (jsonPath.startsWith("$..")) {
            String gflagName = jsonPath.substring(3);
            allSensitiveGflags.add(gflagName);
          }
        }
      }
    } catch (Exception e) {
      log.warn("Error getting sensitive gflags from GFlagsValidation: ", e);
    }

    // Fallback to hardcoded LDAP gflag
    if (allSensitiveGflags.isEmpty()) {
      allSensitiveGflags.add("ycql_ldap_bind_passwd");
    }

    log.debug(
        "Found {} sensitive gflags for redaction: {}",
        allSensitiveGflags.size(),
        allSensitiveGflags);
    return allSensitiveGflags;
  }

  private static Set<String> getSensitiveJsonPathsForVersion(
      String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    try {
      if (gFlagsValidation == null) {
        log.debug("GFlagsValidation is null, cannot get sensitive json paths");
        return null;
      }
      return gFlagsValidation.getSensitiveJsonPathsForVersion(ybSoftwareVersion);
    } catch (Exception e) {
      log.warn("Error getting sensitive gflags for version {}: ", ybSoftwareVersion, e);
      return null;
    }
  }

  private static String getYbSoftwareVersion(Universe universe) {
    if (universe == null) {
      log.debug("Universe is null, cannot get software version");
      return null;
    }
    try {
      return universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    } catch (Exception e) {
      log.warn("Error getting YB software version from universe: ", e);
      return null;
    }
  }

  public static boolean isGFlagRedacted(String value) {
    return value != null && value.contains(RedactingService.SECRET_REPLACEMENT);
  }

  public static String redactString(String input) {
    String length = ((Integer) input.length()).toString();
    String regex = "(.){" + length + "}";
    String output = input.replaceAll(regex, SECRET_REPLACEMENT);
    return output;
  }

  public static String redactQueryParams(String input) {
    String output = input;
    for (String param : SECRET_QUERY_PARAMS_FOR_LOGS) {
      String regex = "([?&]" + param + "=)([^&]+)";
      String replacement = "$1" + SECRET_REPLACEMENT;
      output = output.replaceAll(regex, replacement);
    }
    return output;
  }

  public static String redactrootCAKeys(String input) {
    String output = input;
    Matcher matcher = KEY_SEARCH_PATTERN.matcher(input);
    if (matcher.find()) {
      String base64String = matcher.group(2);
      if (isBase64Encoded(base64String)) {
        String redactedString = "$1: " + SECRET_REPLACEMENT;
        output = output.replaceAll(KEY_REGEX, redactedString);
      }
    }
    return output;
  }

  public static String redactShellProcessOutput(String input, RedactionTarget target) {
    String output = input;
    try {
      // Redact based on target
      switch (target) {
        case QUERY_PARAMS:
          output = redactQueryParams(output);
          break;
        case HELM_VALUES:
          output = redactrootCAKeys(output);
          break;
        default:
          break;
      }
      output = redactSensitiveInfoInString(output);
      return output;
    } catch (Exception e) {
      log.error("Error redacting shell process output", e);
      return input;
    }
  }

  private static boolean isBase64Encoded(String str) {
    try {
      Base64.getDecoder().decode(str);
      return true;
    } catch (IllegalArgumentException e) {
      return false;
    }
  }

  public enum RedactionTarget {
    LOGS,
    APIS,
    QUERY_PARAMS,
    HELM_VALUES;
  }
}

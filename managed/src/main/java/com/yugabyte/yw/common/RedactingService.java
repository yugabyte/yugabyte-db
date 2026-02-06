// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.PathNotFoundException;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
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

  public static final List<String> SECRET_QUERY_PARAMS_FOR_LOGS =
      ImmutableList.of(/* SAS Token */ "sig");

  public static final List<String> SECRET_PATHS_FOR_LOGS =
      ImmutableList.<String>builder()
          .addAll(SECRET_PATHS_FOR_APIS)
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
          .add("$..GCP_CONFIG.private_key_id")
          .add("$..GCP_CONFIG.private_key")
          .add("$..gceApplicationCredentialsPath")
          .add("$..gceApplicationCredentials")
          // Azure client secret
          .add("$..AZURE_CLIENT_SECRET")
          .add("$..CLIENT_SECRET")
          .add("$..azuClientSecret")
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
          .add("$..ycql_ldap_bind_passwd")
          // Dynatrace API token
          .add("$..apiToken")
          .build();

  // List of json paths to any secret fields we want to redact.
  // More on json path format can be found here: https://goessner.net/articles/JsonPath/
  public static final List<JsonPath> SECRET_JSON_PATHS_APIS =
      SECRET_PATHS_FOR_APIS.stream().map(JsonPath::compile).collect(Collectors.toList());

  public static Set<JsonPath> SECRET_JSON_PATHS_LOGS =
      new CopyOnWriteArraySet<>(
          SECRET_PATHS_FOR_LOGS.stream().map(JsonPath::compile).collect(Collectors.toList()));

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
    if (input == null) {
      return null;
    }

    DocumentContext context = JsonPath.parse(input.deepCopy(), AuditService.JSONPATH_CONFIG);

    switch (target) {
      case APIS:
        SECRET_JSON_PATHS_APIS.forEach(
            path -> {
              try {
                context.set(path, SECRET_REPLACEMENT);
              } catch (PathNotFoundException e) {
                log.trace("skip redacting secret path {} - not present", path.getPath());
              }
            });
        break;
      case LOGS:
        SECRET_JSON_PATHS_LOGS.forEach(
            path -> {
              try {
                // Skip ysql_hba_conf_csv to prevent entire flag redaction
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
      return applyRegexRedaction(afterJsonPath, ybSoftwareVersion, gFlagsValidation);
    }

    if (target == RedactionTarget.APIS) {
      // Apply additional redaction for audit additionalDetails
      // This handles gflags in audit API responses
      afterJsonPath = redactAuditAdditionalDetails(afterJsonPath, null, gFlagsValidation);
      // Redact ldapbindpasswd within ysql_hba_conf_csv for API responses
      afterJsonPath = redactLdapBindPasswdInYsqlHbaConf(afterJsonPath);
    }

    return afterJsonPath;
  }

  public static JsonNode applyRegexRedaction(JsonNode input) {
    return applyRegexRedaction(input, null, null);
  }

  public static JsonNode applyRegexRedaction(
      JsonNode input, String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
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
            "Failed to parse redacted JSON, returning original input. Error: {}",
            parseException.getMessage());
        return input;
      }

    } catch (Exception e) {
      log.warn(
          "Failed to apply regex redaction, returning original JsonNode. Error: {}",
          e.getMessage());
      return input;
    }
  }

  // Redacts sensitive information in string content including LDAP passwords and all sensitive
  // gflags.
  public static String redactSensitiveInfoInString(String input) {
    return redactSensitiveInfoInString(input, null, null);
  }

  public static String redactSensitiveInfoInString(
      String input, String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    String output = input;
    if (input == null) {
      return output;
    }

    // Collect all patterns to redact
    Set<String> allPatternsToRedact = new HashSet<>();

    // Add LDAP password related patterns
    allPatternsToRedact.add("ldapbindpasswd");
    allPatternsToRedact.add("ldapBindPassword");
    allPatternsToRedact.add("ycql_ldap_bind_passwd");
    allPatternsToRedact.add("ldapServiceAccountPassword");

    // Add sensitive gflags if GFlagsValidation is available
    if (gFlagsValidation != null) {
      try {
        if (ybSoftwareVersion == null) {
        } else {
          Set<String> sensitiveGflags =
              getSensitiveGflagsForRedaction(ybSoftwareVersion, gFlagsValidation);
          // Filter out ysql_hba_conf_csv from regex redaction
          Set<String> filteredGflags =
              sensitiveGflags.stream()
                  .filter(gflag -> !gflag.equals("ysql_hba_conf_csv"))
                  .collect(Collectors.toSet());
          allPatternsToRedact.addAll(filteredGflags);
        }
      } catch (Exception e) {
        log.warn("Exception in sensitive gflags processing: {}", e);
      }
    }
    // Apply redaction for all patterns
    for (String pattern : allPatternsToRedact) {
      output = redactStringUsingRegexMatching(pattern, output);
    }

    return output;
  }

  private static String redactStringUsingRegexMatching(String pattern, String output) {
    // CSV double-quote escape in JSON: pattern=\"\"value\"\"
    String csvDoubleQuotePattern = "(" + pattern + "=\\\\\"\\\\\")([^\\\\\"]+)(\\\\\"\\\\\")";
    output = output.replaceAll(csvDoubleQuotePattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Plain CSV double-quote escape: pattern=""value""
    String plainDoubleQuotePattern = "(" + pattern + "=\"\")([^\"]+)(\"\")";
    output = output.replaceAll(plainDoubleQuotePattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Escaped quotes in JSON strings: \"pattern=value\"
    String escapedQuotesPattern = "(\\\\\"" + pattern + "=)([^\\\\,\"]+)(\\\\\"?)";
    output = output.replaceAll(escapedQuotesPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // JSON field format: "pattern": "value"
    String jsonFieldPattern = "(\"" + pattern + "\"\\s*:\\s*\")([^\"]+)(\")";
    output = output.replaceAll(jsonFieldPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Double-escaped JSON: \"pattern\":\"value\"
    String doubleEscapedJsonPattern =
        "(\\\\\"" + pattern + "\\\\\"\\s*:\\s*\\\\\")([^\\\\\"]+)(\\\\\")";
    output = output.replaceAll(doubleEscapedJsonPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // JSON field without quotes: "pattern": value
    String jsonFieldPatternNoQuotes =
        "(\"" + pattern + "\"\\s*:\\s*)([^,\\s\\}\\]\"]+)([,\\s\\}\\]])";
    output = output.replaceAll(jsonFieldPatternNoQuotes, "$1" + SECRET_REPLACEMENT + "$3");

    // Python dict format: {'key': 'pattern', 'value': 'value'}
    String pythonDictPattern = "(\\{'key':\\s*'" + pattern + "',\\s*'value':\\s*')([^']+)(')";
    output = output.replaceAll(pythonDictPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Regular quotes in CSV (fallback) - stop at whitespace, comma, or quote
    if (output.contains(pattern + "=") && !output.contains("\\\"" + pattern + "=")) {
      String regularQuotedPattern = "(\"[^\"]*" + pattern + "=)([^,\\s\"]+)([^\"]*\")";
      output = output.replaceAll(regularQuotedPattern, "$1" + SECRET_REPLACEMENT + "$3");
    }

    // Unquoted standalone values
    if (!output.contains("\"" + pattern)) {
      String unquotedPattern = "\\b(" + pattern + "=)([^\\s,\"]+)\\b";
      output = output.replaceAll(unquotedPattern, "$1" + SECRET_REPLACEMENT);
    }

    // Conf file format: pattern=value
    String keyValuePattern = "^(" + pattern + "\\s*=\\s*)([^\\s\\n]+)";
    output = output.replaceAll(keyValuePattern, "$1" + SECRET_REPLACEMENT);

    // Quoted format: pattern="value"
    String quotedValuePattern = "\\b(" + pattern + "\\s*=\\s*\")([^\"]+)(\")";
    output = output.replaceAll(quotedValuePattern, "$1" + SECRET_REPLACEMENT + "$3");

    // CLI flag format: --pattern value or '--pattern', 'value'
    String cliFlagPattern =
        "((?:,\\s*)?['\"]?--" + pattern + "['\"]?(?:,\\s*|\\s+))['\"]?([^\\s,\\n'\"\\]\\}]+)['\"]?";
    output = output.replaceAll(cliFlagPattern, "$1" + SECRET_REPLACEMENT);

    return output;
  }

  // Redacts sensitive gflags in audit additionalDetails section.
  public static JsonNode redactAuditAdditionalDetails(
      JsonNode jsonData, Universe universe, GFlagsValidation gFlagsValidation) {
    try {
      if (jsonData == null) {
        return jsonData;
      }
      if (jsonData.isArray()) {
        // Handle array of audit entries
        for (JsonNode auditEntry : jsonData) {
          redactSingleAuditEntry(auditEntry, universe, gFlagsValidation);
        }
      } else if (jsonData.isObject()) {
        // Handle single audit entry
        redactSingleAuditEntry(jsonData, universe, gFlagsValidation);
      }
    } catch (Exception e) {
      log.warn("Error while redacting audit additionalDetails: {}", e);
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
      if (additionalDetails != null && additionalDetails.has("gflags")) {
        JsonNode gflags = additionalDetails.get("gflags");
        redactGflagsSection(gflags, universe, gFlagsValidation);
      }
    }
  }

  // Redacts ldapbindpasswd within ysql_hba_conf_csv for API responses.
  private static JsonNode redactLdapBindPasswdInYsqlHbaConf(JsonNode input) {
    if (input == null) {
      return input;
    }
    try {
      redactLdapBindPasswdRecursive(input);
      return input;
    } catch (Exception e) {
      log.warn("Error redacting ldapbindpasswd in ysql_hba_conf_csv: {}", e);
      return input;
    }
  }

  // Recursively traverses JSON and redacts ldapbindpasswd in ysql_hba_conf_csv string values.
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
                  if (fieldName.equals("ysql_hba_conf_csv") || fieldName.equals("ysql_hba_conf")) {
                    String originalValue = fieldValue.asText();
                    String redactedValue = redactLdapBindPasswdInString(originalValue);
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

  // Redacts ldapbindpasswd value in a string (handles unquoted, quoted, double-quoted formats).
  private static String redactLdapBindPasswdInString(String input) {
    if (input == null) {
      return input;
    }
    String result = input;
    // Double-quoted CSV escape: ldapbindpasswd=""value""
    result =
        result.replaceAll("(ldapbindpasswd=\"\")([^\"]+)(\"\")", "$1" + SECRET_REPLACEMENT + "$3");
    // Quoted: ldapbindpasswd="value"
    result = result.replaceAll("(ldapbindpasswd=\")([^\"]+)(\")", "$1" + SECRET_REPLACEMENT + "$3");
    // Unquoted: ldapbindpasswd=value (until space, comma, or quote)
    result = result.replaceAll("(ldapbindpasswd=)([^\\s,\"]+)", "$1" + SECRET_REPLACEMENT);
    return result;
  }

  public static void redactGflagsSection(JsonNode gflags) {
    redactGflagsSection(gflags, null, null);
  }

  // Redacts sensitive gflags in gflags section for both master and tserver.
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
              if (sensitiveGflags.contains(flagName)) {
                ObjectNode flagNode = (ObjectNode) flag;
                for (String field : new String[] {"old", "new", "default"}) {
                  if (flag.has(field)) {
                    JsonNode fieldValue = flag.get(field);
                    if (!fieldValue.isNull()
                        && !(fieldValue.isTextual() && fieldValue.asText().isEmpty())) {
                      flagNode.put(field, SECRET_REPLACEMENT);
                    }
                  }
                }
              }
            }
          }
        }
      }
    } catch (Exception e) {
      log.warn("Error while redacting gflags section: {}", e);
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
      log.warn("Error getting sensitive gflags from GFlagsValidation: {}", e);
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
      log.warn("Error getting sensitive gflags for version {}: {}", ybSoftwareVersion, e);
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
      log.warn("Error getting YB software version from universe: {}", e.getMessage());
      return null;
    }
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

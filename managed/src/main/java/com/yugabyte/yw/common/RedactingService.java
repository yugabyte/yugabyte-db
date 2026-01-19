// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.PathNotFoundException;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import java.util.Base64;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArraySet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class RedactingService {

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
    // Pattern 1: Handle escaped quotes within JSON strings
    // Matches: \"pattern=value\" within JSON strings like "ysql_hba_conf_csv"
    String escapedQuotesPattern = "(\\\\\"" + pattern + "=)([^\\\\,\"]+)(\\\\\"?)";
    output = output.replaceAll(escapedQuotesPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Pattern 2: Handle JSON field format "pattern": "value"
    // Handles "ycql_ldap_bind_passwd":"secret12"
    String jsonFieldPattern = "(\"" + pattern + "\"\\s*:\\s*\")([^\"]+)(\")";
    output = output.replaceAll(jsonFieldPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Pattern 2a: Handle double-escaped JSON format within stringified JSON
    // Matches: \"pattern\":\"value\" within JSON strings like universeDetailsJson
    String doubleEscapedJsonPattern =
        "(\\\\\"" + pattern + "\\\\\"\\s*:\\s*\\\\\")([^\\\\\"]+)(\\\\\")";
    output = output.replaceAll(doubleEscapedJsonPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Pattern 2b: Handle JSON field format "pattern": value (without quotes around value)
    String jsonFieldPatternNoQuotes =
        "(\"" + pattern + "\"\\s*:\\s*)([^,\\s\\}\\]\"]+)([,\\s\\}\\]])";
    output = output.replaceAll(jsonFieldPatternNoQuotes, "$1" + SECRET_REPLACEMENT + "$3");

    // Matches: {'key': 'pattern', 'value': 'value'}
    String pythonDictPattern = "(\\{'key':\\s*'" + pattern + "',\\s*'value':\\s*')([^']+)(')";
    output = output.replaceAll(pythonDictPattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Pattern 3: Handle regular quotes within CSV strings (fallback)
    // Only applied if escaped quotes pattern didn't match
    if (output.contains(pattern + "=") && !output.contains("\\\"" + pattern + "=")) {
      String regularQuotedPattern = "(\"[^\"]*" + pattern + "=)([^,\"]+)([^\"]*\")";
      output = output.replaceAll(regularQuotedPattern, "$1" + SECRET_REPLACEMENT + "$3");
    }

    // Pattern 4: Handle unquoted values (standalone)
    // Simplified to avoid complex lookbehind issues
    if (!output.contains("\"" + pattern)) { // Only if not in JSON field context
      String unquotedPattern = "\\b(" + pattern + "=)([^\\s,\"]+)\\b";
      output = output.replaceAll(unquotedPattern, "$1" + SECRET_REPLACEMENT);
    }

    // Pattern 5: Handle conf file format (pattern=value)
    String keyValuePattern = "^(" + pattern + "\\s*=\\s*)([^\\s\\n]+)";
    output = output.replaceAll(keyValuePattern, "$1" + SECRET_REPLACEMENT);

    // Pattern 6: Handle quoted format (pattern="value")
    String quotedValuePattern = "\\b(" + pattern + "\\s*=\\s*\")([^\"]+)(\")";
    output = output.replaceAll(quotedValuePattern, "$1" + SECRET_REPLACEMENT + "$3");

    // Pattern 7: Handle CLI flag format (--pattern, value) and Python list format ('--pattern',
    // 'value')
    String cliFlagPattern =
        "((?:,\\s*)?['\"]?--" + pattern + "['\"]?(?:,\\s*|\\s+))['\"]?([^\\s,\\n'\"\\]\\}]+)['\"]?";
    output = output.replaceAll(cliFlagPattern, "$1" + SECRET_REPLACEMENT);

    return output;
  }

  // Gets all sensitive gflags using the GFlagsValidation class
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
      log.warn(
          "Error getting sensitive gflags from GFlagsValidation, skipping regex redaction: {}", e);
    }
    log.debug(
        "Found {} sensitive gflags for redaction: {}",
        allSensitiveGflags.size(),
        allSensitiveGflags);
    return allSensitiveGflags;
  }

  // Gets sensitive json paths from GFlagsValidation
  private static Set<String> getSensitiveJsonPathsForVersion(
      String ybSoftwareVersion, GFlagsValidation gFlagsValidation) {
    try {
      return gFlagsValidation.getSensitiveJsonPathsForVersion(ybSoftwareVersion);
    } catch (Exception e) {
      log.warn("Error getting sensitive gflags for version {}: {}", ybSoftwareVersion, e);
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

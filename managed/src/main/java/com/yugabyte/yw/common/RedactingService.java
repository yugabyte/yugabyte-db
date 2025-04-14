// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.PathNotFoundException;
import com.yugabyte.yw.common.audit.AuditService;
import java.util.Base64;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArraySet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

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
                context.set(path, SECRET_REPLACEMENT);
              } catch (PathNotFoundException e) {
                log.trace("skip redacting secret path {} - not present", path.getPath());
              }
            });
        break;
      default:
        throw new IllegalArgumentException("Target " + target.name() + " is not supported");
    }

    return context.json();
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

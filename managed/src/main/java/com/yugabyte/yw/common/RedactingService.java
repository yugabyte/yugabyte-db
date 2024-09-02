// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.yugabyte.yw.common.audit.AuditService;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Singleton;

@Singleton
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
          // GCP private key
          .add("$..['config.config_file_contents.private_key_id']")
          .add("$..['config.config_file_contents.private_key']")
          .add("$..config.private_key_id")
          .add("$..config.private_key")
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
          .add("$..ysql_hba_conf_csv")
          .build();

  // List of json paths to any secret fields we want to redact.
  // More on json path format can be found here: https://goessner.net/articles/JsonPath/
  public static final List<JsonPath> SECRET_JSON_PATHS_APIS =
      SECRET_PATHS_FOR_APIS.stream().map(JsonPath::compile).collect(Collectors.toList());

  public static final List<JsonPath> SECRET_JSON_PATHS_LOGS =
      SECRET_PATHS_FOR_LOGS.stream().map(JsonPath::compile).collect(Collectors.toList());

  public static JsonNode filterSecretFields(JsonNode input, RedactionTarget target) {
    if (input == null) {
      return null;
    }

    DocumentContext context = JsonPath.parse(input.deepCopy(), AuditService.JSONPATH_CONFIG);

    switch (target) {
      case APIS:
        SECRET_JSON_PATHS_APIS.forEach(path -> context.set(path, SECRET_REPLACEMENT));
        break;
      case LOGS:
        SECRET_JSON_PATHS_LOGS.forEach(path -> context.set(path, SECRET_REPLACEMENT));
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

  public enum RedactionTarget {
    LOGS,
    APIS;
  }
}

/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import static com.yugabyte.yw.common.Util.HTTPS_SCHEME;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentials;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.core.SdkBytes;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.iam.IamClient;
import software.amazon.awssdk.services.iam.model.GetRoleRequest;
import software.amazon.awssdk.services.iam.model.GetRoleResponse;
import software.amazon.awssdk.services.iam.model.Role;
import software.amazon.awssdk.services.kms.KmsClient;
import software.amazon.awssdk.services.kms.model.*;
import software.amazon.awssdk.services.sts.StsClient;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityRequest;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityResponse;

public class AwsEARServiceUtil {
  private static final String CMK_POLICY = "default_cmk_policy.json";

  // All fields in AWS KMS authConfig object sent from UI
  public enum AwsKmsAuthConfigField {
    ACCESS_KEY_ID("AWS_ACCESS_KEY_ID", true, false),
    SECRET_ACCESS_KEY("AWS_SECRET_ACCESS_KEY", true, false),
    ENDPOINT("AWS_KMS_ENDPOINT", true, false),
    CMK_POLICY("cmk_policy", true, false),
    REGION("AWS_REGION", false, true),
    CMK_ID("cmk_id", false, true);

    public final String fieldName;
    public final boolean isEditable;
    public final boolean isMetadata;

    AwsKmsAuthConfigField(String fieldName, boolean isEditable, boolean isMetadata) {
      this.fieldName = fieldName;
      this.isEditable = isEditable;
      this.isMetadata = isMetadata;
    }

    public static List<String> getEditableFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getNonEditableFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> !configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getMetadataFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> configField.isMetadata)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }
  }

  private static final Logger LOG = LoggerFactory.getLogger(AwsEARServiceUtil.class);

  private static AwsCredentials getCredentials(ObjectNode authConfig) {
    if (!StringUtils.isBlank(
            authConfig.path(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName).asText())
        && !StringUtils.isBlank(
            authConfig.path(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName).asText())) {

      return AwsBasicCredentials.create(
          authConfig.get(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName).asText(),
          authConfig.get(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName).asText());
    }
    return null;
  }

  public static KmsClient getKMSClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return getKMSClient(configUUID, authConfig);
  }

  public static KmsClient getKMSClient(UUID configUUID, ObjectNode authConfig) {
    if (authConfig == null) {
      authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    }

    AwsCredentials awsCredentials = getCredentials(authConfig);
    String region = authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText();

    if (awsCredentials == null || StringUtils.isBlank(region)) {
      return KmsClient.builder().build();
    }

    String endpoint =
        authConfig.path(AwsKmsAuthConfigField.ENDPOINT.fieldName).isMissingNode()
            ? null
            : authConfig.path(AwsKmsAuthConfigField.ENDPOINT.fieldName).asText();

    if (StringUtils.isBlank(endpoint)) {
      return KmsClient.builder()
          .credentialsProvider(StaticCredentialsProvider.create(awsCredentials))
          .region(Region.of(region))
          .build();
    } else {
      try {
        URI uri = new URI(endpoint);
        if (uri.getScheme() == null) {
          uri = new URI(HTTPS_SCHEME + endpoint);
        }
        return KmsClient.builder()
            .credentialsProvider(StaticCredentialsProvider.create(awsCredentials))
            .region(Region.of(region))
            .endpointOverride(uri)
            .build();
      } catch (URISyntaxException e) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid end point: " + e.getMessage());
      }
    }
  }

  public static IamClient getIAMClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AwsCredentials awsCredentials = getCredentials(authConfig);

    if (awsCredentials == null
        || StringUtils.isBlank(authConfig.path(AwsKmsAuthConfigField.REGION.fieldName).asText())) {
      return IamClient.builder().build();
    }

    return IamClient.builder()
        .credentialsProvider(StaticCredentialsProvider.create(awsCredentials))
        .region(Region.of(authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText()))
        .build();
  }

  public static StsClient getSTSClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AwsCredentials awsCredentials = getCredentials(authConfig);
    if (awsCredentials == null
        || StringUtils.isBlank(authConfig.path(AwsKmsAuthConfigField.REGION.fieldName).asText())) {
      return StsClient.builder().build();
    }

    return StsClient.builder()
        .credentialsProvider(StaticCredentialsProvider.create(awsCredentials))
        .region(Region.of(authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText()))
        .build();
  }

  public static ObjectNode getPolicyBase() {
    ObjectNode policy = null;
    try {
      ObjectMapper mapper = new ObjectMapper();
      Environment environment = StaticInjectorHolder.injector().instanceOf(Environment.class);
      policy = (ObjectNode) mapper.readTree(environment.resourceAsStream(CMK_POLICY));
    } catch (Exception e) {
      String errMsg = "Error occurred retrieving default cmk policy base";
      LOG.error(errMsg, e);
    }
    return policy;
  }

  private static ObjectNode bindArnToStatement(ObjectNode statement, String arn) {
    String principalField = "Principal";
    ObjectNode principal = (ObjectNode) statement.get(principalField);
    principal.put("AWS", arn);
    statement.set(principalField, principal);
    return statement;
  }

  public static ObjectNode bindParamsToPolicyBase(
      ObjectNode policyBase, String userArn, String rootArn) {
    String statementField = "Statement";
    ArrayNode statements = (ArrayNode) policyBase.get(statementField);
    statements.set(0, bindArnToStatement((ObjectNode) statements.get(0), rootArn));
    statements.set(1, bindArnToStatement((ObjectNode) statements.get(1), userArn));
    return (ObjectNode) policyBase.set(statementField, statements);
  }

  private static GetCallerIdentityResponse getCallerIdentity(UUID configUUID) {
    GetCallerIdentityRequest req = GetCallerIdentityRequest.builder().build();
    return getSTSClient(configUUID).getCallerIdentity(req);
  }

  private static String getResourceNameFromArn(String arn) {
    return (arn.split(":")[5]).split("/")[1];
  }

  public static String generateDefaultPolicy(UUID configUUID) {
    ObjectNode policyBase = getPolicyBase();
    GetCallerIdentityResponse callerIdentity = getCallerIdentity(configUUID);
    if (callerIdentity != null) {
      String credentialsArn = callerIdentity.arn();
      if (credentialsArn.contains(":assumed-role/")) {
        credentialsArn = getRole(configUUID, getResourceNameFromArn(credentialsArn)).arn();
      } else if (!credentialsArn.contains(":user/")) {
        throw new RuntimeException("Credentials provided are not associated to a user or role");
      }
      String rootArn = String.format("arn:aws:iam::%s:root", callerIdentity.account());
      return bindParamsToPolicyBase(policyBase, credentialsArn, rootArn).toString();
    } else {
      LOG.error("Could not get AWS caller identity from provided credentials");
      return null;
    }
  }

  private static void createAlias(UUID configUUID, String kId, String aliasName) {
    final CreateAliasRequest req =
        CreateAliasRequest.builder().aliasName(aliasName).targetKeyId(kId).build();
    AwsEARServiceUtil.getKMSClient(configUUID).createAlias(req);
  }

  private static void updateAlias(UUID configUUID, String kId, String aliasName) {
    final UpdateAliasRequest req =
        UpdateAliasRequest.builder().aliasName(aliasName).targetKeyId(kId).build();
    AwsEARServiceUtil.getKMSClient(configUUID).updateAlias(req);
  }

  public static void deleteAlias(UUID configUUID, String aliasName) {
    final DeleteAliasRequest req = DeleteAliasRequest.builder().aliasName(aliasName).build();
    AwsEARServiceUtil.getKMSClient(configUUID).deleteAlias(req);
  }

  public static AliasListEntry getAlias(UUID configUUID, String aliasName) {
    ListAliasesRequest req = ListAliasesRequest.builder().limit(100).build();
    AliasListEntry uniAlias = null;
    boolean done = false;
    KmsClient client = AwsEARServiceUtil.getKMSClient(configUUID);
    while (!done) {
      ListAliasesResponse result = client.listAliases(req);
      for (AliasListEntry alias : result.aliases()) {
        if (alias.aliasName().equals(aliasName)) {
          uniAlias = alias;
          done = true;
          break;
        }
      }
      req = ListAliasesRequest.builder().limit(100).marker(result.nextMarker()).build();
      if (!result.truncated()) done = true;
    }
    return uniAlias;
  }

  private static Role getRole(UUID configUUID, String roleName) {
    GetRoleRequest req = GetRoleRequest.builder().roleName(roleName).build();
    GetRoleResponse result = getIAMClient(configUUID).getRole(req);
    return result.role();
  }

  public static DescribeKeyResponse describeKey(ObjectNode authConfig, String cmkId) {
    final DescribeKeyRequest req = DescribeKeyRequest.builder().keyId(cmkId).build();
    return getKMSClient(null, authConfig).describeKey(req);
  }

  public static KeyListEntry getCMK(UUID configUUID, String cmkId) {
    KeyListEntry cmk = null;
    ListKeysRequest.Builder reqBuilder = ListKeysRequest.builder().limit(1000);
    boolean done = false;
    KmsClient client = getKMSClient(configUUID);
    while (!done) {
      ListKeysResponse result = client.listKeys(reqBuilder.build());
      for (KeyListEntry key : result.keys()) {
        if (key.keyId().equals(cmkId)) {
          cmk = key;
          done = true;
          break;
        }
      }
      reqBuilder.marker(result.nextMarker());
      if (!result.truncated()) done = true;
    }
    return cmk;
  }

  public static String getCMKId(UUID configUUID) {
    final ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    final JsonNode cmkNode = authConfig.get(AwsKmsAuthConfigField.CMK_ID.fieldName);
    return cmkNode == null ? null : cmkNode.asText();
  }

  public static String getCMKId(ObjectNode authConfig) {
    final JsonNode cmkNode = authConfig.get(AwsKmsAuthConfigField.CMK_ID.fieldName);
    return cmkNode == null ? null : cmkNode.asText();
  }

  public static byte[] getByteArrayFromBuffer(ByteBuffer byteBuffer) {
    byteBuffer.rewind();
    byte[] byteArray = new byte[byteBuffer.remaining()];
    byteBuffer.get(byteArray);
    return byteArray;
  }

  /**
   * This will only be used at the time of rotating the master key when we need to re-encrypt the
   * universe keys. This is not used at the time of generating new universe keys like the other KMS
   * services.
   *
   * @param configUUID the KMS config UUID.
   * @param plainTextUniverseKey the plaintext universe key to be encrypted.
   * @return the encrypted universe key.
   */
  public static byte[] encryptUniverseKey(UUID configUUID, byte[] plainTextUniverseKey) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return encryptUniverseKey(configUUID, plainTextUniverseKey, authConfig);
  }

  public static byte[] encryptUniverseKey(
      UUID configUUID, byte[] plainTextUniverseKey, ObjectNode authConfig) {
    String cmkId = getCMKId(authConfig);
    final EncryptRequest req =
        EncryptRequest.builder()
            .keyId(cmkId)
            .plaintext(SdkBytes.fromByteArray(plainTextUniverseKey))
            .build();
    final EncryptResponse result = getKMSClient(configUUID, authConfig).encrypt(req);
    return result.ciphertextBlob().asByteArray();
  }

  public static byte[] decryptUniverseKey(
      UUID configUUID, byte[] encryptedUniverseKey, ObjectNode config) {
    if (encryptedUniverseKey == null) return null;
    final DecryptRequest req =
        DecryptRequest.builder()
            .ciphertextBlob(SdkBytes.fromByteArray(encryptedUniverseKey))
            .build();
    final DecryptResponse result = getKMSClient(configUUID, config).decrypt(req);
    return result.plaintext().asByteArray();
  }

  public static byte[] generateDataKey(
      UUID configUUID, String cmkId, String algorithm, int keySize) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return generateDataKey(configUUID, authConfig, cmkId, algorithm, keySize);
  }

  public static byte[] generateDataKey(
      UUID configUUID, ObjectNode authConfig, String cmkId, String algorithm, int keySize) {
    final String keySpecBase = "%s_%s";
    final GenerateDataKeyWithoutPlaintextRequest req =
        GenerateDataKeyWithoutPlaintextRequest.builder()
            .keyId(cmkId)
            .keySpec(String.format(keySpecBase, algorithm, keySize))
            .build();
    final GenerateDataKeyWithoutPlaintextResponse result =
        getKMSClient(configUUID, authConfig).generateDataKeyWithoutPlaintext(req);
    return result.ciphertextBlob().asByteArray();
  }

  public static CreateKeyResponse createCMK(UUID configUUID, String description, String policy) {
    CreateKeyRequest.Builder builder = CreateKeyRequest.builder();
    if (description != null) {
      builder.description(description);
    }
    if (policy != null && policy.length() > 0) {
      builder.policy(policy);
    } else {
      builder.policy(generateDefaultPolicy(configUUID));
    }
    return getKMSClient(configUUID).createKey(builder.build());
  }

  public static void createOrUpdateCMKAlias(UUID configUUID, String cmkId, String aliasBaseName) {
    final String aliasName = generateAliasName(aliasBaseName);
    AliasListEntry existingAlias = getAlias(configUUID, aliasName);
    if (existingAlias == null) {
      createAlias(configUUID, cmkId, aliasName);
    } else if (!existingAlias.targetKeyId().equals(cmkId)) {
      updateAlias(configUUID, cmkId, aliasName);
    }
  }

  public static String generateAliasName(String aliasName) {
    final String aliasNameBase = "alias/%s";
    return String.format(aliasNameBase, aliasName);
  }
}

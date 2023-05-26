/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClientBuilder;
import com.amazonaws.services.identitymanagement.model.GetRoleRequest;
import com.amazonaws.services.identitymanagement.model.GetRoleResult;
import com.amazonaws.services.identitymanagement.model.Role;
import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.AWSKMSClientBuilder;
import com.amazonaws.services.kms.model.AliasListEntry;
import com.amazonaws.services.kms.model.CreateAliasRequest;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DecryptRequest;
import com.amazonaws.services.kms.model.DeleteAliasRequest;
import com.amazonaws.services.kms.model.DescribeKeyRequest;
import com.amazonaws.services.kms.model.DescribeKeyResult;
import com.amazonaws.services.kms.model.EncryptRequest;
import com.amazonaws.services.kms.model.EncryptResult;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextRequest;
import com.amazonaws.services.kms.model.KeyListEntry;
import com.amazonaws.services.kms.model.ListAliasesRequest;
import com.amazonaws.services.kms.model.ListAliasesResult;
import com.amazonaws.services.kms.model.ListKeysRequest;
import com.amazonaws.services.kms.model.ListKeysResult;
import com.amazonaws.services.kms.model.UpdateAliasRequest;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityRequest;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;

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

  private static AWSCredentials getCredentials(ObjectNode authConfig) {

    if (!StringUtils.isBlank(
            authConfig.path(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName).asText())
        && !StringUtils.isBlank(
            authConfig.path(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName).asText())) {

      return new BasicAWSCredentials(
          authConfig.get(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName).asText(),
          authConfig.get(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName).asText());
    }
    return null;
  }

  public static AWSKMS getKMSClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return getKMSClient(configUUID, authConfig);
  }

  public static AWSKMS getKMSClient(UUID configUUID, ObjectNode authConfig) {
    if (authConfig == null) {
      authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    }

    AWSCredentials awsCredentials = getCredentials(authConfig);

    if (awsCredentials == null
        || StringUtils.isBlank(authConfig.path(AwsKmsAuthConfigField.REGION.fieldName).asText())) {

      return AWSKMSClientBuilder.defaultClient();
    }

    if (authConfig.path(AwsKmsAuthConfigField.ENDPOINT.fieldName).isMissingNode()
        || StringUtils.isBlank(
            authConfig.path(AwsKmsAuthConfigField.ENDPOINT.fieldName).asText())) {
      return AWSKMSClientBuilder.standard()
          .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
          .withRegion(authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText())
          .build();
    }

    return AWSKMSClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withEndpointConfiguration(
            new EndpointConfiguration(
                authConfig.path(AwsKmsAuthConfigField.ENDPOINT.fieldName).asText(),
                authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText()))
        .build();
  }

  public static AmazonIdentityManagement getIAMClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AWSCredentials awsCredentials = getCredentials(authConfig);

    if (awsCredentials == null
        || StringUtils.isBlank(authConfig.path(AwsKmsAuthConfigField.REGION.fieldName).asText())) {

      return AmazonIdentityManagementClientBuilder.defaultClient();
    }
    return AmazonIdentityManagementClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withRegion(authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText())
        .build();
  }

  public static AWSSecurityTokenService getSTSClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AWSCredentials awsCredentials = getCredentials(authConfig);
    if (awsCredentials == null
        || StringUtils.isBlank(authConfig.path(AwsKmsAuthConfigField.REGION.fieldName).asText())) {

      return AWSSecurityTokenServiceClientBuilder.defaultClient();
    }
    return AWSSecurityTokenServiceClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withRegion(authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText())
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

  private static GetCallerIdentityResult getCallerIdentity(UUID configUUID) {
    GetCallerIdentityRequest req = new GetCallerIdentityRequest();
    return getSTSClient(configUUID).getCallerIdentity(req);
  }

  private static String getResourceNameFromArn(String arn) {
    return (arn.split(":")[5]).split("/")[1];
  }

  public static String generateDefaultPolicy(UUID configUUID) {
    ObjectNode policyBase = getPolicyBase();
    GetCallerIdentityResult callerIdentity = getCallerIdentity(configUUID);
    if (callerIdentity != null) {
      String credentialsArn = callerIdentity.getArn();
      if (credentialsArn.contains(":assumed-role/")) {
        credentialsArn = getRole(configUUID, getResourceNameFromArn(credentialsArn)).getArn();
      } else if (!credentialsArn.contains(":user/")) {
        throw new RuntimeException("Credentials provided are not associated to a user or role");
      }
      String rootArn = String.format("arn:aws:iam::%s:root", callerIdentity.getAccount());
      return bindParamsToPolicyBase(policyBase, credentialsArn, rootArn).toString();
    } else {
      LOG.error("Could not get AWS caller identity from provided credentials");
      return null;
    }
  }

  private static void createAlias(UUID configUUID, String kId, String aliasName) {
    final CreateAliasRequest req =
        new CreateAliasRequest().withAliasName(aliasName).withTargetKeyId(kId);
    AwsEARServiceUtil.getKMSClient(configUUID).createAlias(req);
  }

  private static void updateAlias(UUID configUUID, String kId, String aliasName) {
    final UpdateAliasRequest req =
        new UpdateAliasRequest().withAliasName(aliasName).withTargetKeyId(kId);
    AwsEARServiceUtil.getKMSClient(configUUID).updateAlias(req);
  }

  public static void deleteAlias(UUID configUUID, String aliasName) {
    final DeleteAliasRequest req = new DeleteAliasRequest().withAliasName(aliasName);
    AwsEARServiceUtil.getKMSClient(configUUID).deleteAlias(req);
  }

  public static AliasListEntry getAlias(UUID configUUID, String aliasName) {
    ListAliasesRequest req = new ListAliasesRequest().withLimit(100);
    AliasListEntry uniAlias = null;
    boolean done = false;
    AWSKMS client = AwsEARServiceUtil.getKMSClient(configUUID);
    while (!done) {
      ListAliasesResult result = client.listAliases(req);
      for (AliasListEntry alias : result.getAliases()) {
        if (alias.getAliasName().equals(aliasName)) {
          uniAlias = alias;
          done = true;
          break;
        }
      }
      req.setMarker(result.getNextMarker());
      if (!result.getTruncated()) done = true;
    }
    return uniAlias;
  }

  private static Role getRole(UUID configUUID, String roleName) {
    GetRoleRequest req = new GetRoleRequest().withRoleName(roleName);
    GetRoleResult result = getIAMClient(configUUID).getRole(req);
    return result.getRole();
  }

  public static DescribeKeyResult describeKey(ObjectNode authConfig, String cmkId) {
    AWSKMS client = AwsEARServiceUtil.getKMSClient(null, authConfig);
    DescribeKeyRequest request = new DescribeKeyRequest().withKeyId(cmkId);
    DescribeKeyResult response = client.describeKey(request);
    return response;
  }

  public static KeyListEntry getCMK(UUID configUUID, String cmkId) {
    KeyListEntry cmk = null;
    ListKeysRequest req = new ListKeysRequest().withLimit(1000);
    boolean done = false;
    AWSKMS client = AwsEARServiceUtil.getKMSClient(configUUID);
    while (!done) {
      ListKeysResult result = client.listKeys(req);
      for (KeyListEntry key : result.getKeys()) {
        if (key.getKeyId().equals(cmkId)) {
          cmk = key;
          done = true;
          break;
        }
      }
      req.setMarker(result.getNextMarker());
      if (!result.getTruncated()) done = true;
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
    AWSKMS client = AwsEARServiceUtil.getKMSClient(configUUID);
    String cmkId = AwsEARServiceUtil.getCMKId(configUUID);
    EncryptRequest request =
        new EncryptRequest().withKeyId(cmkId).withPlaintext(ByteBuffer.wrap(plainTextUniverseKey));
    EncryptResult response = client.encrypt(request);
    return getByteArrayFromBuffer(response.getCiphertextBlob());
  }

  public static byte[] encryptUniverseKey(
      UUID configUUID, byte[] plainTextUniverseKey, ObjectNode authConfig) {
    AWSKMS client = AwsEARServiceUtil.getKMSClient(configUUID, authConfig);
    String cmkId = AwsEARServiceUtil.getCMKId(authConfig);
    EncryptRequest request =
        new EncryptRequest().withKeyId(cmkId).withPlaintext(ByteBuffer.wrap(plainTextUniverseKey));
    EncryptResult response = client.encrypt(request);
    return getByteArrayFromBuffer(response.getCiphertextBlob());
  }

  public static byte[] decryptUniverseKey(
      UUID configUUID, byte[] encryptedUniverseKey, ObjectNode config) {
    if (encryptedUniverseKey == null) return null;
    ByteBuffer encryptedKeyBuffer = ByteBuffer.wrap(encryptedUniverseKey);
    encryptedKeyBuffer.rewind();
    final DecryptRequest req = new DecryptRequest().withCiphertextBlob(encryptedKeyBuffer);
    ByteBuffer decryptedKeyBuffer =
        AwsEARServiceUtil.getKMSClient(configUUID, config).decrypt(req).getPlaintext();
    decryptedKeyBuffer.rewind();
    byte[] decryptedUniverseKey = new byte[decryptedKeyBuffer.remaining()];
    decryptedKeyBuffer.get(decryptedUniverseKey);
    return decryptedUniverseKey;
  }

  public static byte[] generateDataKey(
      UUID configUUID, String cmkId, String algorithm, int keySize) {
    return generateDataKey(configUUID, null, cmkId, algorithm, keySize);
  }

  public static byte[] generateDataKey(
      UUID configUUID, ObjectNode authConfig, String cmkId, String algorithm, int keySize) {
    final String keySpecBase = "%s_%s";
    final GenerateDataKeyWithoutPlaintextRequest dataKeyRequest =
        new GenerateDataKeyWithoutPlaintextRequest()
            .withKeyId(cmkId)
            .withKeySpec(String.format(keySpecBase, algorithm, Integer.toString(keySize)));
    ByteBuffer encryptedKeyBuffer =
        AwsEARServiceUtil.getKMSClient(configUUID, authConfig)
            .generateDataKeyWithoutPlaintext(dataKeyRequest)
            .getCiphertextBlob();
    encryptedKeyBuffer.rewind();
    byte[] encryptedKeyBytes = new byte[encryptedKeyBuffer.remaining()];
    encryptedKeyBuffer.get(encryptedKeyBytes);
    return encryptedKeyBytes;
  }

  public static CreateKeyResult createCMK(UUID configUUID, String description, String policy) {
    CreateKeyRequest req = new CreateKeyRequest();
    if (description != null) req = req.withDescription(description);
    if (policy != null && policy.length() > 0) {
      req = req.withPolicy(policy);
    } else {
      req = req.withPolicy(AwsEARServiceUtil.generateDefaultPolicy(configUUID));
    }
    return AwsEARServiceUtil.getKMSClient(configUUID).createKey(req);
  }

  public static void createOrUpdateCMKAlias(UUID configUUID, String cmkId, String aliasBaseName) {
    final String aliasName = generateAliasName(aliasBaseName);
    AliasListEntry existingAlias = getAlias(configUUID, aliasName);
    if (existingAlias == null) {
      createAlias(configUUID, cmkId, aliasName);
    } else if (!existingAlias.getTargetKeyId().equals(cmkId)) {
      updateAlias(configUUID, cmkId, aliasName);
    }
  }

  public static String generateAliasName(String aliasName) {
    final String aliasNameBase = "alias/%s";
    return String.format(aliasNameBase, aliasName);
  }
}

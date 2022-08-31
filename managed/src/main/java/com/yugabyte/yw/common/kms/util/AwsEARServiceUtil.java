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
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.ebean.annotation.EnumValue;
import java.nio.ByteBuffer;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Application;
import play.api.Play;

public class AwsEARServiceUtil {
  private static final String CMK_POLICY = "default_cmk_policy.json";

  private static final Logger LOG = LoggerFactory.getLogger(AwsEARServiceUtil.class);

  private enum CredentialType {
    @EnumValue("KMS_CONFIG")
    KMS_CONFIG,
    @EnumValue("CLOUD_PROVIDER")
    CLOUD_PROVIDER;
  }

  private static AWSCredentials getCredentials(ObjectNode authConfig) {

    if (!StringUtils.isBlank(authConfig.path("AWS_ACCESS_KEY_ID").asText())
        && !StringUtils.isBlank(authConfig.path("AWS_SECRET_ACCESS_KEY").asText())) {

      return new BasicAWSCredentials(
          authConfig.get("AWS_ACCESS_KEY_ID").asText(),
          authConfig.get("AWS_SECRET_ACCESS_KEY").asText());
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

    if (awsCredentials == null || StringUtils.isBlank(authConfig.path("AWS_REGION").asText())) {

      return AWSKMSClientBuilder.defaultClient();
    }

    if (authConfig.path("AWS_KMS_ENDPOINT").isMissingNode()
        || StringUtils.isBlank(authConfig.path("AWS_KMS_ENDPOINT").asText())) {
      return AWSKMSClientBuilder.standard()
          .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
          .withRegion(authConfig.get("AWS_REGION").asText())
          .build();
    }

    return AWSKMSClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withEndpointConfiguration(
            new EndpointConfiguration(
                authConfig.path("AWS_KMS_ENDPOINT").asText(),
                authConfig.get("AWS_REGION").asText()))
        .build();
  }

  public static AmazonIdentityManagement getIAMClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AWSCredentials awsCredentials = getCredentials(authConfig);

    if (awsCredentials == null || StringUtils.isBlank(authConfig.path("AWS_REGION").asText())) {

      return AmazonIdentityManagementClientBuilder.defaultClient();
    }
    return AmazonIdentityManagementClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withRegion(authConfig.get("AWS_REGION").asText())
        .build();
  }

  public static AWSSecurityTokenService getSTSClient(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);

    AWSCredentials awsCredentials = getCredentials(authConfig);
    if (awsCredentials == null || StringUtils.isBlank(authConfig.path("AWS_REGION").asText())) {

      return AWSSecurityTokenServiceClientBuilder.defaultClient();
    }
    return AWSSecurityTokenServiceClientBuilder.standard()
        .withCredentials(new AWSStaticCredentialsProvider(awsCredentials))
        .withRegion(authConfig.get("AWS_REGION").asText())
        .build();
  }

  public static ObjectNode getPolicyBase() {
    ObjectNode policy = null;
    try {
      ObjectMapper mapper = new ObjectMapper();
      Application application = Play.current().injector().instanceOf(Application.class);
      policy = (ObjectNode) mapper.readTree(application.resourceAsStream(CMK_POLICY));
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
    final String keySpecBase = "%s_%s";
    final GenerateDataKeyWithoutPlaintextRequest dataKeyRequest =
        new GenerateDataKeyWithoutPlaintextRequest()
            .withKeyId(cmkId)
            .withKeySpec(String.format(keySpecBase, algorithm, Integer.toString(keySize)));
    ByteBuffer encryptedKeyBuffer =
        AwsEARServiceUtil.getKMSClient(configUUID)
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

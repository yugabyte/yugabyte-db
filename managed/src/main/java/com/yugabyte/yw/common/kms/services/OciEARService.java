/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.OciAlgorithm;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import java.util.List;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;

public class OciEARService extends EncryptionAtRestService<OciAlgorithm> {
  private final RuntimeConfGetter confGetter;
  private OciEARServiceUtil ociEARServiceUtil;

  public OciEARService(RuntimeConfGetter confGetter) {
    super(KeyProvider.OCI);
    this.confGetter = confGetter;
  }

  public OciEARServiceUtil getOciEarServiceUtil() {
    return new OciEARServiceUtil();
  }

  @Override
  protected OciAlgorithm[] getSupportedAlgorithms() {
    return OciAlgorithm.values();
  }

  @Override
  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode config) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    try {
      // Resolve the key name to a key OCID (creating the key if
      // it does not yet exist) and cache the derived OCID into the auth config for runtime use.
      String keyName =
          ociEARServiceUtil.getSafeText(config, OciKmsAuthConfigField.ociKeyName.fieldName);
      if (StringUtils.isBlank(keyName)) {
        throw new RuntimeException("OCI_KEY_NAME is required");
      }
      // Clear any existing OCID to ensure it is re-resolved from the key name
      config.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
      ociEARServiceUtil.resolveKeyOcid(configUUID, config);

      return config;
    } catch (Exception e) {
      LOG.error("Error creating/validating OCI KMS config", e);
      return null;
    }
  }

  @Override
  protected EncryptionKey createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    String keyOcid = ociEARServiceUtil.getKeyOcid(configUUID);

    if (keyOcid == null) {
      throw new RuntimeException("OCI Key OCID not found in config");
    }

    switch (config.type) {
      case CMK:
        return new EncryptionKey(keyOcid.getBytes());
      default:
      case DATA_KEY:
        byte[] dataKey =
            ociEARServiceUtil.generateDataEncryptionKey(configUUID, authConfig, keyOcid);
        return new EncryptionKey(dataKey);
    }
  }

  @Override
  protected EncryptionKey rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    // Similar to createKeyWithService
    return createKeyWithService(universeUUID, configUUID, config);
  }

  @Override
  public byte[] retrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode encryptionContext) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    try {
      ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
      return ociEARServiceUtil.decryptUniverseKey(configUUID, keyRef, authConfig);
    } catch (Exception e) {
      LOG.error("Error retrieving key from OCI KMS", e);
      throw new RuntimeException("Error retrieving key from OCI KMS", e);
    }
  }

  @Override
  protected byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode encryptionContext, ObjectNode authConfig) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    try {
      return ociEARServiceUtil.decryptUniverseKey(configUUID, keyRef, authConfig);
    } catch (Exception e) {
      LOG.error("Error validating key retrieval from OCI KMS", e);
      return null;
    }
  }

  @Override
  public EncryptionKey encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    try {
      ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
      byte[] encrypted = ociEARServiceUtil.encryptUniverseKey(configUUID, universeKey, authConfig);
      return new EncryptionKey(encrypted);
    } catch (Exception e) {
      LOG.error("Error encrypting key with OCI KMS", e);
      throw new RuntimeException("Error encrypting key with OCI KMS", e);
    }
  }

  @Override
  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {
    // No cleanup needed for OCI KMS
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    ociEARServiceUtil.validateKMSProviderConfigFormData(authConfig);

    // Re-resolve the key OCID from the key name (clearing any cached OCID) so that a key which was
    // re-created under the same name is picked up on refresh.
    authConfig.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    String keyOcid = ociEARServiceUtil.resolveKeyOcid(configUUID, authConfig);
    if (StringUtils.isNotBlank(keyOcid)
        && !ociEARServiceUtil.validateKeySettings(authConfig, keyOcid)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Key does not have valid settings in OCI KMS config '%s'.", configUUID));
    }
    ociEARServiceUtil.testWrapAndUnwrapKey(configUUID, authConfig);
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    List<String> metadataFields = OciKmsAuthConfigField.getMetadataFields();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    ObjectNode keyMetadata = new ObjectMapper().createObjectNode();

    for (String fieldName : metadataFields) {
      if (authConfig.has(fieldName)) {
        keyMetadata.set(fieldName, authConfig.get(fieldName));
      }
    }
    keyMetadata.put("key_provider", KeyProvider.OCI.name());
    return keyMetadata;
  }
}

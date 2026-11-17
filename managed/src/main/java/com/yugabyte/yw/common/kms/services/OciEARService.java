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
import com.yugabyte.yw.cloud.oci.OCIErrorFormatter;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.OciAlgorithm;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
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
      // The key is chosen by name only here, at create time; the OCID recorded now identifies the
      // key for the life of the config. A client-supplied OCID is ignored: it was not validated.
      config.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
      ociEARServiceUtil.resolveOrCreateKeyOcid(configUUID, config);

      return config;
    } catch (Exception e) {
      LOG.error("Error creating/validating OCI KMS config", e);
      return null;
    }
  }

  @Override
  public KmsConfig updateAuthConfig(UUID configUUID, ObjectNode config) {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    try {
      // Universe keys are wrapped with the stored key, so an edit must not switch to another one.
      ObjectNode updatedConfig = config.deepCopy();
      String storedKeyOcid =
          ociEARServiceUtil.getSafeText(
              getAuthConfig(configUUID), OciKmsAuthConfigField.ociKeyOcid.fieldName);
      if (StringUtils.isBlank(storedKeyOcid)) {
        updatedConfig.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
      } else {
        updatedConfig.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, storedKeyOcid);
      }
      String keyOcid = ociEARServiceUtil.resolveKeyOcid(configUUID, updatedConfig);
      if (!ociEARServiceUtil.validateKeySettings(updatedConfig, keyOcid)) {
        throw new RuntimeException(
            String.format(
                "OCI key '%s' has invalid settings: it must be in Enabled state and use AES"
                    + " algorithm.",
                keyOcid));
      }
      return KmsConfig.updateKMSConfig(configUUID, updatedConfig);
    } catch (Exception e) {
      LOG.error("Error updating OCI KMS config", e);
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
      String context = "Error retrieving key from OCI KMS";
      LOG.error(context, e);
      throw new RuntimeException(
          OCIErrorFormatter.isOciError(e) ? OCIErrorFormatter.userMessage(context, e) : context, e);
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
      String context = "Error encrypting key with OCI KMS";
      LOG.error(context, e);
      throw new RuntimeException(
          OCIErrorFormatter.isOciError(e) ? OCIErrorFormatter.userMessage(context, e) : context, e);
    }
  }

  @Override
  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {
    // No cleanup needed for OCI KMS
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    this.ociEARServiceUtil = getOciEarServiceUtil();
    // Validates the stored key OCID, or for a config without one the key found by name.
    ociEARServiceUtil.validateKMSProviderConfigFormData(authConfig);
    try {
      // Fails only for a config without a stored OCID whose named key is gone.
      ociEARServiceUtil.resolveKeyOcid(configUUID, authConfig);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
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

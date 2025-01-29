package com.yugabyte.yw.common.kms.services;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.CipherTrustAlgorithm;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil.CipherTrustKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.CiphertrustManagerClient;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CiphertrustEARService extends EncryptionAtRestService<CipherTrustAlgorithm> {
  public static final int numBytes = 32;

  public CiphertrustEARService(RuntimeConfGetter confGetter) {
    super(KeyProvider.CIPHERTRUST);
  }

  public CiphertrustEARServiceUtil getCiphertrustEARServiceUtil() {
    return new CiphertrustEARServiceUtil();
  }

  @Override
  protected CipherTrustAlgorithm[] getSupportedAlgorithms() {
    return CipherTrustAlgorithm.values();
  }

  @Override
  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode config) {
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();
    try {
      UUID customerUUID = KmsConfig.getOrBadRequest(configUUID).getCustomerUUID();
      CiphertrustManagerClient ciphertrustManagerClient =
          ciphertrustEARServiceUtil.getCiphertrustManagerClient(config);

      // Check if a key with the given name exists on CipherTrust manager
      boolean keyExists = ciphertrustEARServiceUtil.checkifKeyExists(config);
      if (keyExists) {
        log.info(
            "Key already exists on CipherTrust manager. Using the existing key '{}'.",
            ciphertrustEARServiceUtil.getConfigFieldValue(
                config, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName));
      } else {
        log.info(
            "Key does not exist on CipherTrust manager. Creating a new key '{}'.",
            ciphertrustEARServiceUtil.getConfigFieldValue(
                config, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName));
        // Create a new key on CipherTrust manager.
        String newKeyName = ciphertrustEARServiceUtil.createKey(config);
      }

      // Update the authConfig with the correct key details.
      ciphertrustEARServiceUtil.updateAuthConfigFromKeyDetails(config);
      UpdateAuthConfigProperties(customerUUID, configUUID, config);
      log.info(
          "Updated authConfig from key details for CipherTrust KMS configUUID '{}'.", configUUID);

      // Test the encryption and decryption of a random key for the KMS config key.
      ciphertrustEARServiceUtil.testEncryptAndDecrypt(config);
      log.info(
          "Successfully tested encryption and decryption of a random key for CipherTrust KMS"
              + " configUUID '{}'.",
          configUUID.toString());

      return config;
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error attempting to create or retrieve Key in CIPHERTRUST KMS with config %s.",
              configUUID.toString());
      LOG.error(errMsg, e);
      return null;
    }
    // return config;
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    return null;
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    return null;
  }

  @Override
  public byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef) {
    return null;
  }

  @Override
  public byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    return null;
  }

  @Override
  protected byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode authConfig) {
    return null;
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) {
    // TODO: Implement this method.
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    return null;
  }
}

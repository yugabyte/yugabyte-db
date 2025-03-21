package com.yugabyte.yw.common.kms.services;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.CipherTrustAlgorithm;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil.CipherTrustKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import java.util.List;
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

      // Validate the key settings.
      boolean areKeySettingsValid = ciphertrustEARServiceUtil.validateKeySettings(config);
      if (!areKeySettingsValid) {
        log.error(
            "Key settings for key '{}' are invalid for CipherTrust KMS configUUID '{}'.",
            ciphertrustEARServiceUtil.getConfigFieldValue(
                config, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName),
            configUUID);
        return null;
      }

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
  protected EncryptionKey createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    // Ensure the key exists on the CipherTrust manager.
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    boolean keyExists = ciphertrustEARServiceUtil.checkifKeyExists(authConfig);
    if (!keyExists) {
      String errMsg =
          String.format(
              "Key does not exist on CipherTrust manager. Cannot create a new key for universe"
                  + " '%s'.",
              universeUUID);
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }

    byte[] keyBytes = ciphertrustEARServiceUtil.generateRandomBytes(numBytes);
    return ciphertrustEARServiceUtil.encryptKeyWithEncryptionContext(authConfig, keyBytes);
  }

  @Override
  protected EncryptionKey rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    return createKeyWithService(universeUUID, configUUID, config);
  }

  @Override
  public byte[] retrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode encryptionContext) {
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return ciphertrustEARServiceUtil.decryptKeyWithEncryptionContext(authConfig, encryptionContext);
  }

  @Override
  public EncryptionKey encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return ciphertrustEARServiceUtil.encryptKeyWithEncryptionContext(authConfig, universeKey);
  }

  @Override
  protected byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode encryptionContext, ObjectNode authConfig) {
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();
    log.info("Validating retreive key with KMS config '{}'.", configUUID);
    return ciphertrustEARServiceUtil.decryptKeyWithEncryptionContext(authConfig, encryptionContext);
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    CiphertrustEARServiceUtil ciphertrustEARServiceUtil = getCiphertrustEARServiceUtil();

    // Validate the KMS provider config form data for required fields.
    log.info(
        "Validating KMS provider config form data for CipherTrust KMS configUUID '{}'.",
        configUUID);
    ciphertrustEARServiceUtil.validateKMSProviderConfigFormData(authConfig);

    // Validate the key settings.
    boolean areKeySettingsValid = ciphertrustEARServiceUtil.validateKeySettings(authConfig);
    if (!areKeySettingsValid) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Key settings for key '%s' are invalid for CipherTrust KMS configUUID '%s'.",
              ciphertrustEARServiceUtil.getConfigFieldValue(
                  authConfig, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName),
              configUUID));
    }
    log.info(
        "Key settings for key '{}' are valid for CipherTrust KMS configUUID '{}'.",
        ciphertrustEARServiceUtil.getConfigFieldValue(
            authConfig, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName),
        configUUID);

    // Test the encryption and decryption of a random key for the KMS config key.
    ciphertrustEARServiceUtil.testEncryptAndDecrypt(authConfig);
    log.info(
        "Successfully tested encryption and decryption of a random key for CipherTrust KMS"
            + " configUUID '{}'.",
        configUUID.toString());
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    // Get all the auth config fields marked as metadata.
    List<String> cipherTrustKmsMetadataFields = CipherTrustKmsAuthConfigField.getMetadataFields();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    ObjectNode keyMetadata = new ObjectMapper().createObjectNode();

    for (String fieldName : cipherTrustKmsMetadataFields) {
      if (authConfig.has(fieldName)) {
        keyMetadata.set(fieldName, authConfig.get(fieldName));
      }
    }
    // Add key_provider field.
    keyMetadata.put("key_provider", KeyProvider.CIPHERTRUST.name());
    return keyMetadata;
  }
}

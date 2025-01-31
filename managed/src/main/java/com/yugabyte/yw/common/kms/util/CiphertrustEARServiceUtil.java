package com.yugabyte.yw.common.kms.util;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CiphertrustEARServiceUtil {

  // All fields in Azure KMS authConfig object sent from UI
  public enum CipherTrustKmsAuthConfigField {
    CIPHERTRUST_MANAGER_URL("CIPHERTRUST_MANAGER_URL", false, true),
    REFRESH_TOKEN("REFRESH_TOKEN", true, false),
    KEY_NAME("KEY_NAME", false, true),
    KEY_ALGORITHM("KEY_ALGORITHM", false, true),
    KEY_SIZE("KEY_SIZE", false, true);

    public final String fieldName;
    public final boolean isEditable;
    public final boolean isMetadata;

    CipherTrustKmsAuthConfigField(String fieldName, boolean isEditable, boolean isMetadata) {
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

  public CustomCAStoreManager getCustomCAStoreManager() {
    return StaticInjectorHolder.injector().instanceOf(CustomCAStoreManager.class);
  }

  public CiphertrustManagerClient getCiphertrustManagerClient(ObjectNode authConfig) {
    CustomCAStoreManager customCAStoreManager = getCustomCAStoreManager();

    // Get the base URL for the Ciphertrust Manager from auth config.
    String baseUrl =
        authConfig.path(CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName).asText();
    // Get the refresh token for the Ciphertrust Manager from auth config.
    String refreshToken =
        authConfig.path(CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName).asText();
    // Get the key name for the Ciphertrust Manager from auth config.
    String keyName = authConfig.path(CipherTrustKmsAuthConfigField.KEY_NAME.fieldName).asText();

    // Get the key algorithm for the Ciphertrust Manager from auth config.
    String keyAlgorithm =
        authConfig
            .path(CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName)
            .asText()
            .toLowerCase();

    // Get the key size for the Ciphertrust Manager from auth config.
    int keySize = authConfig.path(CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName).asInt();

    return new CiphertrustManagerClient(
        baseUrl,
        refreshToken,
        customCAStoreManager.getYbaAndJavaKeyStore(),
        keyName,
        keyAlgorithm,
        keySize);
  }

  public boolean checkifKeyExists(ObjectNode authConfig) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    Map<String, Object> keyDetails = ciphertrustManagerClient.getKeyDetails();
    if (keyDetails == null || keyDetails.isEmpty()) {
      // Key does not exist
      return false;
    } else {
      return true;
    }
  }

  public String createKey(ObjectNode authConfig) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    String keyName = ciphertrustManagerClient.createKey();
    if (keyName == null || keyName.isEmpty()) {
      log.error("Error creating key with CIPHERTRUST KMS.");
      return null;
    } else {
      return keyName;
    }
  }

  public byte[] generateRandomBytes(int numBytes) {
    byte[] randomBytes = new byte[numBytes];
    try {
      SecureRandom.getInstanceStrong().nextBytes(randomBytes);
    } catch (NoSuchAlgorithmException e) {
      log.warn("Could not generate CIPHERTRUST random bytes, no such algorithm.");
      return null;
    }
    return randomBytes;
  }

  public Map<String, Object> encryptKey(ObjectNode authConfig, byte[] keyBytes) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    // Recheck the conversion from bytes -> string once again.
    Map<String, Object> encryptResponse =
        ciphertrustManagerClient.encryptText(Base64.getEncoder().encodeToString(keyBytes));
    if (encryptResponse == null || encryptResponse.isEmpty()) {
      log.error("Error encrypting key with CIPHERTRUST KMS.");
      return null;
    }
    return encryptResponse;
  }

  public byte[] decryptKey(ObjectNode authConfig, Map<String, Object> encryptedKeyMaterial) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    // Recheck the conversion from bytes -> string once again.
    String decryptResponse = ciphertrustManagerClient.decryptText(encryptedKeyMaterial);
    if (decryptResponse == null || decryptResponse.isEmpty()) {
      log.error("Error decrypting key with CIPHERTRUST KMS.");
      return null;
    }
    return Base64.getDecoder().decode(decryptResponse);
  }

  public String getConfigFieldValue(ObjectNode authConfig, String fieldName) {
    String fieldValue = "";
    if (authConfig.has(fieldName)) {
      fieldValue = authConfig.path(fieldName).asText();
    } else {
      log.warn(
          String.format(
              "Could not get '%s' from CipherTrust authConfig. '%s' not found.",
              fieldName, fieldName));
      return null;
    }
    return fieldValue;
  }

  public void updateAuthConfigFromKeyDetails(ObjectNode authConfig) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    Map<String, Object> keyDetails = ciphertrustManagerClient.getKeyDetails();
    if (keyDetails == null || keyDetails.isEmpty()) {
      log.error("Error getting key details from CIPHERTRUST KMS.");
      return;
    }

    // Get the key algorithm and size from the auth config and the actual key details.
    String authConfigKeyAlgorithm =
        keyDetails
            .getOrDefault(CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName, "")
            .toString();
    int authConfigKeySize =
        Integer.parseInt(
            keyDetails
                .getOrDefault(CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName, 0)
                .toString());

    String actualKeyAlgorithm = keyDetails.getOrDefault("algorithm", "").toString();
    int actualKeySize = Integer.parseInt(keyDetails.getOrDefault("size", 0).toString());

    // Update the auth config with the actual key algorithm and size if any mismatch.
    if (!actualKeyAlgorithm.isEmpty() && !actualKeyAlgorithm.equals(authConfigKeyAlgorithm)) {
      authConfig.put(CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName, actualKeyAlgorithm);
      log.warn(
          "Key algorithm mismatch between auth config and actual key in CIPHERTRUST KMS. Changed"
              + " key algorithm from {} to {} in the KMS config.",
          authConfigKeyAlgorithm,
          actualKeyAlgorithm);
    }
    if (actualKeySize != 0 && actualKeySize != authConfigKeySize) {
      authConfig.put(CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName, actualKeySize);
      log.warn(
          "Key size mismatch between auth config and actual key in CIPHERTRUST KMS. Changed key"
              + " size from {} to {} in the KMS config.",
          authConfigKeySize,
          actualKeySize);
    }
  }

  public void testEncryptAndDecrypt(ObjectNode authConfig) {
    byte[] randomTestKey = generateRandomBytes(32);
    Map<String, Object> encryptedKeyMaterial = encryptKey(authConfig, randomTestKey);
    byte[] decryptedKeyBytes = decryptKey(authConfig, encryptedKeyMaterial);
    if (!Arrays.equals(randomTestKey, decryptedKeyBytes)) {
      String errMsg = "Encrypt and decrypt operations gave different outputs in CipherTrust KMS.";
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }

  public void validateKMSProviderConfigFormData(ObjectNode formData) throws Exception {
    // Required fields.
    List<String> fieldsList =
        Arrays.asList(
            CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName,
            CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName,
            CipherTrustKmsAuthConfigField.KEY_NAME.fieldName,
            CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName,
            CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName);
    List<String> fieldsNotPresent =
        fieldsList.stream()
            .filter(
                field ->
                    !formData.has(field) || StringUtils.isBlank(formData.path(field).toString()))
            .collect(Collectors.toList());
    if (!fieldsNotPresent.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "CipherTrust KMS missing the required fields: " + fieldsNotPresent.toString());
    }
  }
}

package com.yugabyte.yw.common.kms.util;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.kms.util.CiphertrustManagerClient.AuthType;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.ArrayList;
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
    AUTH_TYPE("AUTH_TYPE", true, true),
    REFRESH_TOKEN("REFRESH_TOKEN", true, false),
    USERNAME("USERNAME", true, false),
    PASSWORD("PASSWORD", true, false),
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

    // Get the auth type for the Ciphertrust Manager from auth config.
    AuthType authTypeEnum =
        AuthType.valueOf(
            authConfig.path(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName).asText());

    String refreshToken = "";
    String username = "";
    String password = "";
    if (AuthType.REFRESH_TOKEN.equals(authTypeEnum)) {
      // Get the refresh token for the Ciphertrust Manager from auth config.
      refreshToken =
          authConfig.path(CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName).asText();
    } else if (AuthType.PASSWORD.equals(authTypeEnum)) {
      // Get the username for the Ciphertrust Manager from auth config.
      username = authConfig.path(CipherTrustKmsAuthConfigField.USERNAME.fieldName).asText();

      // Get the password for the Ciphertrust Manager from auth config.
      password = authConfig.path(CipherTrustKmsAuthConfigField.PASSWORD.fieldName).asText();
    }

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
        authTypeEnum,
        refreshToken,
        username,
        password,
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

  public EncryptionKey encryptKeyWithEncryptionContext(ObjectNode authConfig, byte[] keyBytes) {
    Map<String, Object> encryptResponse = encryptKey(authConfig, keyBytes);
    Object cipherTextObject = encryptResponse.get("ciphertext");
    String cipherTextString = "";
    if (cipherTextObject != null && cipherTextObject instanceof String) {
      cipherTextString = cipherTextObject.toString();
    }
    if (cipherTextString.isEmpty()) {
      log.error("Error encrypting key with CIPHERTRUST KMS. Got empty ciphertext.");
      return null;
    }
    byte[] cipherTextBytes = Base64.getDecoder().decode(cipherTextString);
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode encryptionContext = mapper.convertValue(encryptResponse, ObjectNode.class);
    return new EncryptionKey(cipherTextBytes, encryptionContext);
  }

  public Map<String, Object> encryptKey(ObjectNode authConfig, byte[] keyBytes) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    // Recheck the conversion from bytes -> string once again.
    Map<String, Object> encryptResponse =
        ciphertrustManagerClient.encryptText(Base64.getEncoder().encodeToString(keyBytes));
    if (encryptResponse == null
        || encryptResponse.isEmpty()
        || !encryptResponse.containsKey("ciphertext")) {
      log.error("Error encrypting key with CIPHERTRUST KMS.");
      return null;
    }
    return encryptResponse;
  }

  public String getKeyId(ObjectNode authConfig) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    Map<String, Object> keyDetails = ciphertrustManagerClient.getKeyDetails();
    if (keyDetails == null || keyDetails.isEmpty()) {
      // Key does not exist
      log.error("Key does not exist on CipherTrust manager.");
      return null;
    }
    if (keyDetails.containsKey("id")) {
      String keyId = keyDetails.get("id").toString();
      if (keyId == null || keyId.isEmpty()) {
        log.error("Key ID is empty.");
        return null;
      } else {
        log.info(
            "Key name is: '{}', key ID is '{}'.",
            authConfig.path(CipherTrustKmsAuthConfigField.KEY_NAME.fieldName).asText(),
            keyId);
        return keyId;
      }
    } else {
      log.error("Key ID not found in key details.");
      return null;
    }
  }

  public byte[] decryptKeyWithEncryptionContext(
      ObjectNode authConfig, ObjectNode encryptionContext) {
    Map<String, Object> encryptedKeyMaterial =
        new ObjectMapper().convertValue(encryptionContext, Map.class);
    return decryptKey(authConfig, encryptedKeyMaterial);
  }

  public byte[] decryptKey(ObjectNode authConfig, Map<String, Object> encryptedKeyMaterial) {
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    // Check if the key name and ID correspond to the same key.
    String keyId = getKeyId(authConfig);
    if (keyId == null || keyId.isEmpty()) {
      log.error("Key ID is empty in Ciphertrust KMS.");
      return null;
    }
    String keyName = authConfig.path(CipherTrustKmsAuthConfigField.KEY_NAME.fieldName).asText();
    if (keyName == null || keyName.isEmpty()) {
      log.error("Key name is empty in Ciphertrust KMS.");
      return null;
    }
    if (encryptedKeyMaterial.containsKey("id")
        && !keyId.equals(encryptedKeyMaterial.get("id").toString())) {
      String errMsg =
          String.format(
              "Key ID '%s' from the encryption context does not match the key name '%s' in the"
                  + " CIPHERTRUST KMS auth config.",
              keyId, keyName);
      log.error(errMsg);
      return null;
    }
    // Recheck the conversion from bytes -> string once again.
    String decryptResponse = ciphertrustManagerClient.decryptText(encryptedKeyMaterial);
    if (decryptResponse == null || decryptResponse.isEmpty()) {
      log.error("Error decrypting key with CIPHERTRUST KMS.");
      return null;
    }
    return Base64.getDecoder().decode(decryptResponse);
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

  public void validateKMSProviderConfigFormData(ObjectNode formData) throws Exception {
    // Required fields.
    List<String> requiredFields =
        new ArrayList<>(
            Arrays.asList(
                CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName,
                CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName,
                CipherTrustKmsAuthConfigField.KEY_NAME.fieldName,
                CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName,
                CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName));

    // Fields that should not be present based on the auth type.
    List<String> fieldsThatShouldNotBePresent = new ArrayList<>();

    // Check if auth type is present or not first.
    if (!formData.has(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName)
        || StringUtils.isBlank(
            formData.path(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName).toString())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "CipherTrust KMS missing the required field: 'AUTH_TYPE'.");
    }

    // Check the auth type and required fields for those.
    AuthType authTypeEnum =
        AuthType.valueOf(formData.path(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName).asText());
    if (AuthType.REFRESH_TOKEN.equals(authTypeEnum)) {
      requiredFields.add(CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName);
      fieldsThatShouldNotBePresent.add(CipherTrustKmsAuthConfigField.USERNAME.fieldName);
      fieldsThatShouldNotBePresent.add(CipherTrustKmsAuthConfigField.PASSWORD.fieldName);
    } else if (AuthType.PASSWORD.equals(authTypeEnum)) {
      requiredFields.add(CipherTrustKmsAuthConfigField.USERNAME.fieldName);
      requiredFields.add(CipherTrustKmsAuthConfigField.PASSWORD.fieldName);
      fieldsThatShouldNotBePresent.add(CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName);
    }

    List<String> fieldsNotPresent =
        requiredFields.stream()
            .filter(
                field ->
                    !formData.has(field) || StringUtils.isBlank(formData.path(field).toString()))
            .collect(Collectors.toList());
    if (!fieldsNotPresent.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "CipherTrust KMS missing the required fields: " + fieldsNotPresent.toString());
    }

    List<String> fieldsThatShouldNotBePresentButAre =
        fieldsThatShouldNotBePresent.stream()
            .filter(
                field ->
                    formData.has(field) && !StringUtils.isBlank(formData.path(field).toString()))
            .collect(Collectors.toList());
    if (!fieldsThatShouldNotBePresentButAre.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "CipherTrust KMS should not have the following fields: "
              + fieldsThatShouldNotBePresentButAre.toString());
    }
  }

  public boolean validateKeySettings(ObjectNode authConfig) {
    // Check if the key exists on the CipherTrust manager.
    boolean keyExists = checkifKeyExists(authConfig);
    if (!keyExists) {
      log.error(
          "Key '{}' does not exist on CipherTrust manager.",
          getConfigFieldValue(authConfig, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName));
      return false;
    }

    // Get the key details.
    CiphertrustManagerClient ciphertrustManagerClient = getCiphertrustManagerClient(authConfig);
    Map<String, Object> keyDetails = ciphertrustManagerClient.getKeyDetails();

    // Check the key state is active.
    if (keyDetails.containsKey("state")
        && !keyDetails.get("state").toString().toLowerCase().equals("active")) {
      log.error(
          "Key '{}' is not in active state on CipherTrust manager. Actual state = '{}'.",
          getConfigFieldValue(authConfig, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName),
          keyDetails.get("state").toString());
      return false;
    }

    // Check if encrypt and decrypt permissions are present on the key.
    // In the usage mask, 4 = encrypt, 8 = decrypt, 12 = both.
    if (keyDetails.containsKey("usageMask")) {
      int usageMask = Integer.parseInt(keyDetails.get("usageMask").toString());
      if (((usageMask & (1 << 2)) == 0) || ((usageMask & (1 << 3)) == 0)) {
        log.error(
            "Key '{}' does not have encrypt and decrypt permissions on CipherTrust manager. Actual"
                + " usage mask = '{}'.",
            getConfigFieldValue(authConfig, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName),
            keyDetails.get("usageMask").toString());
        return false;
      }
    }

    // If all checks pass, return true.
    return true;
  }
}

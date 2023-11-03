/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.gax.core.CredentialsProvider;
import com.google.api.gax.core.FixedCredentialsProvider;
import com.google.api.gax.rpc.NotFoundException;
import com.google.api.services.cloudresourcemanager.CloudResourceManager;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsRequest;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsResponse;
import com.google.api.services.iam.v1.IamScopes;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.auth.oauth2.ServiceAccountCredentials;
import com.google.cloud.kms.v1.CryptoKey;
import com.google.cloud.kms.v1.CryptoKey.CryptoKeyPurpose;
import com.google.cloud.kms.v1.CryptoKeyName;
import com.google.cloud.kms.v1.CryptoKeyVersion;
import com.google.cloud.kms.v1.CryptoKeyVersionTemplate;
import com.google.cloud.kms.v1.DecryptResponse;
import com.google.cloud.kms.v1.KeyManagementServiceClient;
import com.google.cloud.kms.v1.KeyManagementServiceSettings;
import com.google.cloud.kms.v1.KeyRing;
import com.google.cloud.kms.v1.KeyRingName;
import com.google.cloud.kms.v1.LocationName;
import com.google.cloud.kms.v1.ProtectionLevel;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.PlatformServiceException;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class GcpEARServiceUtil {

  // bare minimum permissions required for using GCP KMS in YBA
  public static final List<String> minRequiredPermissionsList =
      Arrays.asList(
          "cloudkms.keyRings.get",
          "cloudkms.cryptoKeys.get",
          "cloudkms.cryptoKeyVersions.useToEncrypt",
          "cloudkms.cryptoKeyVersions.useToDecrypt",
          "cloudkms.locations.generateRandomBytes");

  // All fields in Google KMS authConfig object sent from UI
  public enum GcpKmsAuthConfigField {
    GCP_CONFIG("GCP_CONFIG", true, false),
    LOCATION_ID("LOCATION_ID", false, true),
    PROTECTION_LEVEL("PROTECTION_LEVEL", false, false),
    GCP_KMS_ENDPOINT("GCP_KMS_ENDPOINT", false, false),
    KEY_RING_ID("KEY_RING_ID", false, true),
    CRYPTO_KEY_ID("CRYPTO_KEY_ID", false, true);

    public final String fieldName;
    public final boolean isEditable;
    public final boolean isMetadata;

    GcpKmsAuthConfigField(String fieldName, boolean isEditable, boolean isMetadata) {
      this.fieldName = fieldName;
      this.isEditable = isEditable;
      this.isMetadata = isMetadata;
    }

    public static List<String> getEditableFields() {
      return Arrays.stream(values())
          .filter(configField -> configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getNonEditableFields() {
      return Arrays.stream(values())
          .filter(configField -> !configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getMetadataFields() {
      return Arrays.stream(values())
          .filter(configField -> configField.isMetadata)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }
  }

  public ObjectNode getAuthConfig(UUID configUUID) {
    return EncryptionAtRestUtil.getAuthConfig(configUUID);
  }

  /**
   * Creates the credentials provider object with the GCP config given by the service account JSON.
   *
   * @param authConfig the config object containing service account details, keyRing resource name,
   *     etc.
   * @return the created credentials provider object
   */
  public CredentialsProvider getCredentialsProvider(ObjectNode authConfig) {
    CredentialsProvider credentialsProvider = null;
    if (authConfig.has(GcpKmsAuthConfigField.GCP_CONFIG.fieldName)
        && !StringUtils.isBlank(
            authConfig.path(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).toString())) {
      String credentials = authConfig.path(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).toString();
      try {
        ServiceAccountCredentials serviceAccountCredentials =
            ServiceAccountCredentials.fromStream(new ByteArrayInputStream(credentials.getBytes()));
        credentialsProvider = FixedCredentialsProvider.create(serviceAccountCredentials);
      } catch (IOException e) {
        log.error("Error while reading GCP credentials:", e);
      }
    }
    return credentialsProvider;
  }

  /**
   * Creates a GCP KMS client object with the GCP config given. Including additional optional
   * parameters.
   *
   * @param authConfig the config object containing service account details, keyRing resource name,
   *     etc.
   * @return a KMS client object
   */
  KeyManagementServiceClient getKMSClient(ObjectNode authConfig) throws IOException {
    KeyManagementServiceSettings.Builder keyManagementServiceSettingsBuilder =
        KeyManagementServiceSettings.newBuilder();

    CredentialsProvider credentialsProvider = getCredentialsProvider(authConfig);
    if (credentialsProvider != null) {
      keyManagementServiceSettingsBuilder.setCredentialsProvider(credentialsProvider);
    }
    if (authConfig.has(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName)
        && !StringUtils.isBlank(
            authConfig.path(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName).asText())) {
      keyManagementServiceSettingsBuilder.setEndpoint(
          authConfig.path(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName).asText());
    }

    return KeyManagementServiceClient.create(keyManagementServiceSettingsBuilder.build());
  }

  /**
   * Gets the project ID from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the project ID
   */
  public String getConfigProjectId(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.GCP_CONFIG.fieldName)
        && authConfig.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).has("project_id")) {
      return authConfig.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).get("project_id").asText();
    } else {
      log.error("Could not get GCP config project ID. 'GCP_CONFIG.project_id' not found.");
      return null;
    }
  }

  /**
   * Gets the GCP KMS endpoint from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS endpoint
   */
  public String getConfigEndpoint(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName)) {
      return authConfig.path(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName).asText();
    } else {
      log.error(
          "Could not get GCP config endpoint from auth config. 'GCP_KMS_ENDPOINT' not found.");
      return null;
    }
  }

  /**
   * Gets the GCP KMS key ring id from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS key ring id
   */
  public String getConfigKeyRingId(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.KEY_RING_ID.fieldName)) {
      return authConfig.path(GcpKmsAuthConfigField.KEY_RING_ID.fieldName).asText();
    } else {
      log.error("Could not get GCP config key ring from auth config. 'KEY_RING_ID' not found.");
      return null;
    }
  }

  /**
   * Gets the GCP KMS crypto key id from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS crypto key id
   */
  public String getConfigCryptoKeyId(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName)) {
      return authConfig.path(GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName).asText();
    } else {
      log.error(
          "Could not get GCP config crypto key ID from auth config. 'CRYPTO_KEY_ID' not found.");
      return null;
    }
  }

  /**
   * Gets the location ID from the config object. Location ID is a single string, can be a
   * single-region or multi-region.
   *
   * @param authConfig the gcp auth config object
   * @return the location ID. Ex: "global", "asia", "us-east1"
   */
  public String getConfigLocationId(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.LOCATION_ID.fieldName)) {
      return authConfig.path(GcpKmsAuthConfigField.LOCATION_ID.fieldName).asText();
    } else {
      log.error("Could not get GCP config location ID from auth config. 'LOCATION_ID' not found.");
      return null;
    }
  }

  public ProtectionLevel getConfigProtectionLevel(ObjectNode authConfig) {
    if (authConfig.has(GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName)) {
      return ProtectionLevel.valueOf(
          authConfig.path(GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName).asText());
    } else {
      log.error(
          "Could not get GCP config protection level from auth config. "
              + "'GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName' not found.");
      return null;
    }
  }

  /**
   * Gets the KeyRing object from the config object. KeyRing must exist already.
   *
   * @param authConfig the gcp auth config object
   * @return the key ring object
   */
  public KeyRing getKeyRing(ObjectNode authConfig) {
    KeyRing keyRing = null;
    String keyRingRN = getKeyRingRN(authConfig);

    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      keyRing = client.getKeyRing(keyRingRN);
    } catch (NotFoundException e) {
      log.error(
          String.format("Key Ring '%s' does not exist on the GCP KMS provider\n", keyRingRN), e);
    } catch (IOException e) {
      log.error(e.getMessage(), e);
    }
    return keyRing;
  }

  /**
   * Gets the key ring resource name from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the key ring resource name. Ex:
   *     "projects/project_name/locations/global/keyRings/yb-keyring"
   */
  public String getKeyRingRN(ObjectNode authConfig) {
    String projectId = getConfigProjectId(authConfig);
    String locationId = getConfigLocationId(authConfig);
    String keyRingId = getConfigKeyRingId(authConfig);
    return KeyRingName.of(projectId, locationId, keyRingId).toString();
  }

  /**
   * Gets the crypto key object from the config object. Crypto key must exist already.
   *
   * @param authConfig the gcp auth config object
   * @return the crypto key object
   */
  public CryptoKey getCryptoKey(ObjectNode authConfig) {
    CryptoKey cryptoKey = null;
    String cryptoKeyRN = getCryptoKeyRN(authConfig);

    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      cryptoKey = client.getCryptoKey(cryptoKeyRN);
    } catch (NotFoundException e) {
      log.error(
          String.format("Crypto Key '%s' does not exist on the GCP KMS provider\n", cryptoKeyRN));
    } catch (IOException e) {
      log.error(e.getMessage(), e);
    }
    return cryptoKey;
  }

  public String getCryptoKeyRN(
      String projectId, String locationId, String keyRingId, String cryptoKeyId) {
    return CryptoKeyName.of(projectId, locationId, keyRingId, cryptoKeyId).toString();
  }

  public String getCryptoKeyRN(ObjectNode authConfig) {
    String projectId = getConfigProjectId(authConfig);
    String locationId = getConfigLocationId(authConfig);
    String keyRingId = getConfigKeyRingId(authConfig);
    String cryptoKeyId = getConfigCryptoKeyId(authConfig);
    return getCryptoKeyRN(projectId, locationId, keyRingId, cryptoKeyId);
  }

  /**
   * Gets the primary crypto key version ID from the config object. Version ID just a number. It is
   * different from the entire resource name.
   *
   * @param authConfig the gcp auth config object
   * @return the crypto key version ID. Ex: "2"
   */
  public String getPrimaryKeyVersionId(ObjectNode authConfig) throws IOException {
    String cryptoKeyRN = getCryptoKeyRN(authConfig);
    String keyVersionRN = getPrimaryKeyVersion(authConfig, cryptoKeyRN).getName();
    return getKeyVersionIdFromRN(keyVersionRN);
  }

  /**
   * Splits the string and retrieves the key ring ID from the entire resource name.
   *
   * @param resourceName the input resource name. Ex:
   *     "projects/project_name/locations/global/keyRings/yb-keyring
   *     /cryptoKeys/yb-key/cryptoKeyVersions/2"
   * @return the keyring ID. Ex: "yb-keyring"
   */
  public String getKeyRingIdFromRN(String resourceName) {
    resourceName += "/";
    int indexFrom = StringUtils.ordinalIndexOf(resourceName, "/", 5);
    int indexTill = StringUtils.ordinalIndexOf(resourceName, "/", 6);
    return resourceName.substring(indexFrom + 1, indexTill);
  }

  /**
   * Splits the string and retrieves the crypto key ID from the entire resource name.
   *
   * @param resourceName the input resource name. Ex:
   *     "projects/project_name/locations/global/keyRings/yb-keyring
   *     /cryptoKeys/yb-key/cryptoKeyVersions/2"
   * @return the cryptokey ID. Ex: "yb-key"
   */
  public String getCryptoKeyIdFromRN(String resourceName) {
    resourceName += "/";
    int indexFrom = StringUtils.ordinalIndexOf(resourceName, "/", 7);
    int indexTill = StringUtils.ordinalIndexOf(resourceName, "/", 8);
    return resourceName.substring(indexFrom + 1, indexTill);
  }

  /**
   * Splits the string and retrieves the crypto key version ID from the entire resource name.
   *
   * @param resourceName the input resource name. Ex:
   *     "projects/project_name/locations/global/keyRings/yb-keyring
   *     /cryptoKeys/yb-key/cryptoKeyVersions/2"
   * @return the keyversion ID. Ex: "2"
   */
  public String getKeyVersionIdFromRN(String resourceName) {
    resourceName += "/";
    int indexFrom = StringUtils.ordinalIndexOf(resourceName, "/", 9);
    int indexTill = StringUtils.ordinalIndexOf(resourceName, "/", 10);
    return resourceName.substring(indexFrom + 1, indexTill);
  }

  /**
   * Gets the location name object from the config object. Needs the location ID and project ID.
   *
   * @param authConfig the gcp auth config object
   * @return the location name object
   */
  public LocationName getLocationName(ObjectNode authConfig) {
    String locationId = getConfigLocationId(authConfig);
    String projectId = getConfigProjectId(authConfig);
    return LocationName.of(projectId, locationId);
  }

  public CryptoKeyVersion getPrimaryKeyVersion(ObjectNode authConfig, String cryptoKeyRN)
      throws IOException {
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      CryptoKey cryptoKey = client.getCryptoKey(cryptoKeyRN);
      return cryptoKey.getPrimary();
    }
  }

  /**
   * Verifies if a key ring exists on the GCP KMS for the client created with the config object.
   *
   * @param authConfig the gcp auth config object
   * @return true if key ring exists, else false.
   */
  public boolean checkKeyRingExists(ObjectNode authConfig) {
    return getKeyRing(authConfig) != null;
  }

  /**
   * Verifies if a crypto key resource name exists on the GCP KMS provider for the client created
   * with the config object.
   *
   * @param authConfig the gcp auth config object
   * @return true if crypto key exists, else false.
   */
  public boolean checkCryptoKeyExists(ObjectNode authConfig) {
    String cryptoKeyRN = getCryptoKeyRN(authConfig);
    if (!checkKeyRingExists(authConfig)) {
      log.info(
          String.format("Key ring doesn't exist, while checking for crypto key '%s'", cryptoKeyRN));
      return false;
    }
    CryptoKey cryptoKey = getCryptoKey(authConfig);
    if (cryptoKey == null) {
      return false;
    }
    // Sets the correct protection level for existing crypto key
    CryptoKeyVersion cryptoKeyVersion = cryptoKey.getPrimary();
    ProtectionLevel configProtectionLevel = getConfigProtectionLevel(authConfig);
    if (!cryptoKeyVersion.getProtectionLevel().equals(configProtectionLevel)) {
      log.info(
          "Found existing key with different protection level. "
              + "Changed protection level from {} to {}.",
          configProtectionLevel,
          cryptoKeyVersion.getProtectionLevel());
      authConfig.put(
          GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName,
          cryptoKeyVersion.getProtectionLevel().toString());
    }
    return true;
  }

  /**
   * Tests both the encrypt and decrypt operations with fake data. Mostly used for testing the
   * permissions.
   *
   * @param authConfig the gcp auth config object
   * @throws RuntimeException if wrap and unwrap operations don't give same output as original
   */
  public void testWrapAndUnwrapKey(ObjectNode authConfig) throws Exception {
    byte[] fakeKeyBytes = new byte[32];
    new Random().nextBytes(fakeKeyBytes);
    byte[] wrappedKey = encryptBytes(authConfig, fakeKeyBytes);
    byte[] unwrappedKey = decryptBytes(authConfig, wrappedKey);
    if (!Arrays.equals(fakeKeyBytes, unwrappedKey)) {
      String errMsg = "Wrap and unwrap operations gave different key in GCP KMS.";
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }

  /**
   * Validates the given crypto key settings. Checks if it exists, has a rotation period, verifies
   * the purpose, and if it is enabled.
   *
   * @param authConfig the gcp auth config object
   * @return true if all the settings of the given crypto key are valid, else false
   */
  public boolean validateCryptoKeySettings(ObjectNode authConfig) {
    if (!checkCryptoKeyExists(authConfig)) {
      log.error("Crypto key doesn't exist while validating crypto key settings.");
      return false;
    }
    CryptoKey cryptoKey = getCryptoKey(authConfig);
    if (cryptoKey.hasRotationPeriod()) {
      log.error("Crypto key has a rotation period. Please set it to manual rotation (never).");
      return false;
    }
    if (cryptoKey.getPurpose() != CryptoKeyPurpose.ENCRYPT_DECRYPT) {
      log.error("Crypto key purpose is not 'ENCRYPT_DECRYPT'. Please set correctly.");
      return false;
    }
    CryptoKeyVersion cryptoKeyVersion = cryptoKey.getPrimary();
    if (cryptoKeyVersion.getState() != CryptoKeyVersion.CryptoKeyVersionState.ENABLED) {
      log.error("Primary crypto key version is not enabled. Please enable it.");
      return false;
    }
    return true;
  }

  /**
   * Creates a new key ring with the given ID. Must not already exist.
   *
   * @param authConfig the gcp auth config object
   * @return the newly created key ring object
   */
  public KeyRing createKeyRing(ObjectNode authConfig) throws IOException {
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      // Build the key ring to create.
      KeyRing keyRing = KeyRing.newBuilder().build();
      // Create the key ring.
      LocationName locationName = getLocationName(authConfig);
      String keyRingId = getConfigKeyRingId(authConfig);
      return client.createKeyRing(locationName, keyRingId, keyRing);
    }
  }

  /**
   * Generates a random sequence of bytes using the Google KMS API. This is used as the universe
   * key. Will be encrypted by using the crypto key created on GCP KMS and stored locally.
   *
   * @param authConfig the gcp auth config object
   * @param numBytes the number of bytes to be generated
   * @return the randomly generated byte array
   */
  public byte[] generateRandomBytes(ObjectNode authConfig, int numBytes) throws IOException {
    LocationName locationName = getLocationName(authConfig);
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      return client
          .generateRandomBytes(locationName.toString(), numBytes, ProtectionLevel.HSM)
          .getData()
          .toByteArray();
    }
  }

  /**
   * Creates a new crypto key with the given key ring resource name and ID. Key ring must already
   * exist. Crypto key must not already exist.
   *
   * @param authConfig the gcp auth config object
   * @param keyRingRN the key ring resource name
   * @param cryptoKeyId the crypto key to be created
   * @return the crypto key object
   */
  public CryptoKey createCryptoKey(ObjectNode authConfig, String keyRingRN, String cryptoKeyId)
      throws IOException {
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      KeyRingName keyRingName;

      // Check the key ring resource name syntax.
      if (KeyRingName.isParsableFrom(keyRingRN)) {
        keyRingName = KeyRingName.parse(keyRingRN);
      } else {
        log.error("Could not parse Key Ring Name from key ring resource name: " + keyRingRN);
        return null;
      }

      // Build the symmetric key with custom parameters.
      CryptoKey.Builder keyBuilder = CryptoKey.newBuilder();
      keyBuilder.setPurpose(CryptoKeyPurpose.ENCRYPT_DECRYPT);

      // Set the protection level to either HSM or default SOFTWARE
      CryptoKeyVersionTemplate.Builder cryptoKeyVersionTemplateBuilder =
          CryptoKeyVersionTemplate.newBuilder();
      if (authConfig.has(GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName)
          && authConfig
              .path(GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName)
              .asText()
              .equals("HSM")) {
        cryptoKeyVersionTemplateBuilder.setProtectionLevel(ProtectionLevel.HSM);
      }
      // Use default google symmetric encryption algorithm
      cryptoKeyVersionTemplateBuilder.setAlgorithm(
          CryptoKeyVersion.CryptoKeyVersionAlgorithm.GOOGLE_SYMMETRIC_ENCRYPTION);
      keyBuilder.setVersionTemplate(cryptoKeyVersionTemplateBuilder);

      // Remove automatic rotation
      keyBuilder.clearRotationPeriod();
      keyBuilder.clearNextRotationTime();
      CryptoKey key = keyBuilder.build();

      // Create the key
      return client.createCryptoKey(keyRingName, cryptoKeyId, key);
    }
  }

  /**
   * Create a key ring with a given ID if it doesn't already exist. Does nothing if already exists.
   *
   * @param authConfig the gcp auth config object
   * @throws Exception when keyring doesn't exist and no 'cloudkms.keyRings.create' permission
   */
  public void checkOrCreateKeyRing(ObjectNode authConfig) throws Exception {
    if (!checkKeyRingExists(authConfig)) {
      List<String> createKeyRingPermissions = Collections.singletonList("cloudkms.keyRings.create");
      if (!testGcpPermissions(authConfig, createKeyRingPermissions)) {
        throw new Exception(
            "Key ring does not already exist and "
                + "service account does not have 'cloudkms.keyRings.create' permission.");
      }
      createKeyRing(authConfig);
    }
  }

  /**
   * Create a crypto key with a given crypto key ID in the config object if it doesn't already
   * exist. Does nothing if crypto key already exists. Key ring ID must already exist.
   *
   * @param authConfig the gcp auth config object
   * @throws Exception when cryptokey doesn't exist and no 'cloudkms.cryptoKeys.create' permission
   */
  public void checkOrCreateCryptoKey(ObjectNode authConfig) throws Exception {
    String cryptoKeyId = getConfigCryptoKeyId(authConfig);
    String keyRingRN = getKeyRingRN(authConfig);
    if (!checkCryptoKeyExists(authConfig)) {
      List<String> createCryptoKeyPermissions =
          Collections.singletonList("cloudkms.cryptoKeys.create");
      if (!testGcpPermissions(authConfig, createCryptoKeyPermissions)) {
        throw new Exception(
            "Crypto key does not already exist and "
                + "service account does not have 'cloudkms.cryptoKeys.create' permission.");
      }
      createCryptoKey(authConfig, keyRingRN, cryptoKeyId);
    }
  }

  /**
   * Encrypts a byte array with the crypto key. Crypto key is formed from the key ring id and crypto
   * key id stored in the config object. Each universe with the same KMS config, uses the same
   * master key (crypto key), but different universe keys that are generated explicitly.
   *
   * @param authConfig the gcp auth config object
   * @param textToEncrypt the sequence of bytes to be encrypted. Generally used to encrypt the
   *     generated universe key.
   * @return the encrypted byte array
   */
  public byte[] encryptBytes(ObjectNode authConfig, byte[] textToEncrypt) throws IOException {
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      String cryptoKeyRN = getCryptoKeyRN(authConfig);
      return client
          .encrypt(cryptoKeyRN, ByteString.copyFrom(textToEncrypt))
          .getCiphertext()
          .toByteArray();
    }
  }

  /**
   * Decrypts a byte array with the crypto key. Crypto key is formed from the key ring id and crypto
   * key id stored in the config object.
   *
   * @param textToDecrypt the encrypted sequence of bytes, to be decrypted
   * @param authConfig the config object
   * @return the decrypted byte array
   */
  public byte[] decryptBytes(ObjectNode authConfig, byte[] textToDecrypt) throws IOException {
    try (KeyManagementServiceClient client = getKMSClient(authConfig)) {
      String cryptoKeyRN = getCryptoKeyRN(authConfig);
      DecryptResponse decryptResponse =
          client.decrypt(cryptoKeyRN, ByteString.copyFrom(textToDecrypt));
      return decryptResponse.getPlaintext().toByteArray();
    }
  }

  /**
   * Verifies whether the service account has enough permissions on the keyrings, cryptokeys, etc.
   * Used in pre-flight validation check at the time of config creation.
   *
   * @param authConfig the config object with all details (credentials, location ID, etc.)
   * @param permissionsList the list of permissions to check if allowed for the service acc
   * @return true if it has enough permission, else false
   */
  public boolean testGcpPermissions(ObjectNode authConfig, List<String> permissionsList) {
    String projectId = getConfigProjectId(authConfig);

    CloudResourceManager service;
    try {
      service = createCloudResourceManagerService(authConfig);
    } catch (IOException | GeneralSecurityException e) {
      log.error("Unable to initialize service: \n", e);
      return false;
    }

    TestIamPermissionsRequest requestBody =
        new TestIamPermissionsRequest().setPermissions(permissionsList);
    try {
      TestIamPermissionsResponse testIamPermissionsResponse =
          service.projects().testIamPermissions(projectId, requestBody).execute();

      if (permissionsList.size() == testIamPermissionsResponse.getPermissions().size()) {
        log.info(
            "Verified that the user has following permissions required for GCP KMS: "
                + String.join(", ", permissionsList));
        return true;
      }
    } catch (IOException e) {
      log.error("Unable to test permissions: \n", e);
      return false;
    }
    log.warn(
        "The user / service account does not have all of the "
            + "following permissions required for GCP KMS: "
            + String.join(", ", permissionsList));
    return false;
  }

  public CloudResourceManager createCloudResourceManagerService(ObjectNode authConfig)
      throws IOException, GeneralSecurityException {
    String credentials = authConfig.path(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).toString();

    GoogleCredentials credential =
        GoogleCredentials.fromStream(new ByteArrayInputStream(credentials.getBytes()))
            .createScoped(Collections.singleton(IamScopes.CLOUD_PLATFORM));

    return new CloudResourceManager.Builder(
            GoogleNetHttpTransport.newTrustedTransport(),
            GsonFactory.getDefaultInstance(),
            new HttpCredentialsAdapter(credential))
        .setApplicationName("service-accounts")
        .build();
  }

  /**
   * Checks if the required fields exist in the request body of the API call.
   *
   * @param formData
   * @return true if all required fields present, else false.
   */
  public boolean checkFieldsExist(ObjectNode formData) {
    List<String> fieldsList =
        Arrays.asList(
            GcpKmsAuthConfigField.GCP_CONFIG.fieldName,
            GcpKmsAuthConfigField.LOCATION_ID.fieldName,
            GcpKmsAuthConfigField.KEY_RING_ID.fieldName,
            GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName);
    for (String fieldKey : fieldsList) {
      if (!formData.has(fieldKey) || StringUtils.isBlank(formData.path(fieldKey).toString())) {
        return false;
      }
    }
    return true;
  }

  /**
   * Validates the entire config form data. Is a wrapper for all other validation checks.
   *
   * @param formData the input form data object
   * @throws Exception
   */
  public void validateKMSProviderConfigFormData(ObjectNode formData) throws Exception {
    // Verify GCP config, location ID, key ring id, and crypto key id are present. Required
    // fields.
    if (!checkFieldsExist(formData)) {
      throw new Exception("Invalid config, location id, keyring id, or crypto key id");
    }
    // Try to create a KMS client with specified fields / options
    try (KeyManagementServiceClient client = getKMSClient(formData)) {
      if (client == null) {
        log.warn("validateKMSProviderConfigFormData: Got KMS Client = null");
        throw new RuntimeException("Invalid GCP KMS parameters");
      }
    }

    // Check if custom key ring id and crypto key id is given
    if (checkFieldsExist(formData)) {
      String keyRingId = formData.path(GcpKmsAuthConfigField.KEY_RING_ID.fieldName).asText();
      log.info("validateKMSProviderConfigFormData: Checked all required fields exist.");
      // If given a custom key ring, validate its permissions
      if (!testGcpPermissions(formData, minRequiredPermissionsList)) {
        log.info(
            "validateKMSProviderConfigFormData: Not enough permissions for key ring = "
                + keyRingId);
        throw new Exception(
            "User does not have enough permissions or unable to test for permissions");
      }
    }
  }
}

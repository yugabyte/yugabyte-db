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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.gax.core.CredentialsProvider;
import com.google.api.gax.core.FixedCredentialsProvider;
import com.google.api.services.cloudresourcemanager.CloudResourceManager;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsRequest;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsResponse;
import com.google.api.services.iam.v1.IamScopes;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.auth.oauth2.ServiceAccountCredentials;
import com.google.cloud.kms.v1.CryptoKey;
import com.google.cloud.kms.v1.CryptoKeyName;
import com.google.cloud.kms.v1.CryptoKeyVersion;
import com.google.cloud.kms.v1.CryptoKeyVersionName;
import com.google.cloud.kms.v1.CryptoKeyVersionTemplate;
import com.google.cloud.kms.v1.DecryptResponse;
import com.google.cloud.kms.v1.EncryptResponse;
import com.google.cloud.kms.v1.GenerateRandomBytesResponse;
import com.google.cloud.kms.v1.KeyManagementServiceClient;
import com.google.cloud.kms.v1.KeyManagementServiceSettings;
import com.google.cloud.kms.v1.KeyRing;
import com.google.cloud.kms.v1.KeyRingName;
import com.google.cloud.kms.v1.LocationName;
import com.google.cloud.kms.v1.ProtectionLevel;
import com.google.cloud.kms.v1.CryptoKey.CryptoKeyPurpose;
import com.google.cloud.kms.v1.CryptoKeyVersion.CryptoKeyVersionAlgorithm;
import com.google.cloud.kms.v1.CryptoKeyVersion.CryptoKeyVersionState;
import com.google.cloud.kms.v1.KeyManagementServiceClient.ListCryptoKeysPagedResponse;
import com.google.cloud.kms.v1.KeyManagementServiceClient.ListKeyRingsPagedResponse;
import com.google.protobuf.ByteString;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;

import lombok.extern.slf4j.Slf4j;

@Slf4j
public class GcpEARServiceUtil {

  public static final String GCP_CONFIG_FIELDNAME = "GCP_CONFIG";
  public static final String LOCATION_ID_FIELDNAME = "LOCATION_ID";
  public static final String PROTECTION_LEVEL_FIELDNAME = "PROTECTION_LEVEL";
  public static final String GCP_KMS_ENDPOINT_FIELDNAME = "GCP_KMS_ENDPOINT";
  public static final String KEY_RING_ID_FIELDNAME = "KEY_RING_ID";
  public static final String CRYPTO_KEY_ID_FIELDNAME = "CRYPTO_KEY_ID";

  public ObjectNode getAuthConfig(UUID configUUID) {
    return EncryptionAtRestUtil.getAuthConfig(configUUID, KeyProvider.GCP);
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
    if (authConfig.has(GCP_CONFIG_FIELDNAME)
        && !StringUtils.isBlank(authConfig.path(GCP_CONFIG_FIELDNAME).toString())) {
      String credentials = authConfig.path(GCP_CONFIG_FIELDNAME).toString();
      try {
        ServiceAccountCredentials serviceAccountCredentials =
            ServiceAccountCredentials.fromStream(new ByteArrayInputStream(credentials.getBytes()));
        credentialsProvider = FixedCredentialsProvider.create(serviceAccountCredentials);
      } catch (IOException e) {
        log.info("Error while reading GCP credentials.");
        e.printStackTrace();
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
  public KeyManagementServiceClient getKMSClient(ObjectNode authConfig) {
    KeyManagementServiceSettings.Builder keyManagementServiceSettingsBuilder =
        KeyManagementServiceSettings.newBuilder();

    CredentialsProvider credentialsProvider = getCredentialsProvider(authConfig);
    if (credentialsProvider != null) {
      keyManagementServiceSettingsBuilder.setCredentialsProvider(credentialsProvider);
    }
    if (authConfig.has(GCP_KMS_ENDPOINT_FIELDNAME)
        && !StringUtils.isBlank(authConfig.path(GCP_KMS_ENDPOINT_FIELDNAME).asText())) {
      keyManagementServiceSettingsBuilder.setEndpoint(
          authConfig.path(GCP_KMS_ENDPOINT_FIELDNAME).asText());
    }
    KeyManagementServiceSettings keyManagementServiceSettings;
    KeyManagementServiceClient gcpKmsClient = null;
    try {
      keyManagementServiceSettings = keyManagementServiceSettingsBuilder.build();
      gcpKmsClient = KeyManagementServiceClient.create(keyManagementServiceSettings);
      log.info("getKMSClient: Finished create client");
    } catch (IOException e) {
      log.info("Error while creating GCP KMS Client");
      e.printStackTrace();
    }
    return gcpKmsClient;
  }

  /**
   * Gets the project ID from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the project ID
   */
  public String getConfigProjectId(ObjectNode authConfig) {
    String projectId = "";
    if (authConfig.has(GCP_CONFIG_FIELDNAME)
        && authConfig.get(GCP_CONFIG_FIELDNAME).has("project_id")) {
      projectId = authConfig.get(GCP_CONFIG_FIELDNAME).get("project_id").asText();
    } else {
      log.info("Could not get GCP config project ID. 'GCP_CONFIG.project_id' not found.");
      return null;
    }
    return projectId;
  }

  /**
   * Gets the GCP KMS endpoint from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS endpoint
   */
  public String getConfigEndpoint(ObjectNode authConfig) {
    String endpoint = "";
    if (authConfig.has(GCP_KMS_ENDPOINT_FIELDNAME)) {
      endpoint = authConfig.path(GCP_KMS_ENDPOINT_FIELDNAME).asText();
    } else {
      log.info("Could not get GCP config endpoint from auth config. 'GCP_KMS_ENDPOINT' not found.");
      return null;
    }
    return endpoint;
  }

  /**
   * Gets the GCP KMS key ring id from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS key ring id
   */
  public String getConfigKeyRingId(ObjectNode authConfig) {
    String keyRingId = "";
    if (authConfig.has(KEY_RING_ID_FIELDNAME)) {
      keyRingId = authConfig.path(KEY_RING_ID_FIELDNAME).asText();
    } else {
      log.info("Could not get GCP config key ring from auth config. 'KEY_RING_ID' not found.");
      return null;
    }
    return keyRingId;
  }

  /**
   * Gets the GCP KMS crypto key id from the config object.
   *
   * @param authConfig the gcp auth config object
   * @return the GCP KMS crypto key id
   */
  public String getConfigCryptoKeyId(ObjectNode authConfig) {
    String cryptoKeyId = "";
    if (authConfig.has(CRYPTO_KEY_ID_FIELDNAME)) {
      cryptoKeyId = authConfig.path(CRYPTO_KEY_ID_FIELDNAME).asText();
    } else {
      log.info(
          "Could not get GCP config crypto key ID from auth config. 'CRYPTO_KEY_ID' not found.");
      return null;
    }
    return cryptoKeyId;
  }

  /**
   * Gets the location ID from the config object. Location ID is a single string, can be a
   * single-region or multi-region.
   *
   * @param authConfig the gcp auth config object
   * @return the location ID. Ex: "global", "asia", "us-east1"
   */
  public String getConfigLocationId(ObjectNode authConfig) {
    String locationId = "";
    if (authConfig.has(LOCATION_ID_FIELDNAME)) {
      locationId = authConfig.path(LOCATION_ID_FIELDNAME).asText();
    } else {
      log.info("Could not get GCP config location ID from auth config. 'LOCATION_ID' not found.");
      return null;
    }
    return locationId;
  }

  /**
   * Gets the KeyRing object from the config object. KeyRing must exist already.
   *
   * @param authConfig the gcp auth config object
   * @return the key ring object
   */
  public KeyRing getKeyRing(ObjectNode authConfig) {
    String keyRingRN = getKeyRingRN(authConfig);
    KeyManagementServiceClient client = getKMSClient(authConfig);
    KeyRing keyRing = client.getKeyRing(keyRingRN);
    client.close();
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
    String CryptoKeyRN = getCryptoKeyRN(authConfig);
    KeyManagementServiceClient client = getKMSClient(authConfig);
    CryptoKey cryptoKey = client.getCryptoKey(CryptoKeyRN);
    client.close();
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
   * Gets the crypto key version resource name from the config, the crypto key resource name, and
   * the key version ID number.
   *
   * @param authConfig the gcp auth config object
   * @param cryptoKeyRN the crypto key resource name
   * @param keyVersionId the crypto key version ID
   * @return the crypto key version resource name. Ex:
   *     "projects/project_name/locations/global/keyRings/yb-keyring
   *     /cryptoKeys/yb-key/cryptoKeyVersions/2"
   */
  public String getKeyVersionRN(ObjectNode authConfig, String cryptoKeyRN, String keyVersionId) {
    String projectId = getConfigProjectId(authConfig);
    String locationId = getConfigLocationId(authConfig);
    String keyRingId = getKeyRingIdFromRN(cryptoKeyRN);
    String cryptoKeyId = getCryptoKeyIdFromRN(cryptoKeyRN);
    return CryptoKeyVersionName.of(projectId, locationId, keyRingId, cryptoKeyId, keyVersionId)
        .toString();
  }

  /**
   * Gets the primary crypto key version ID from the config object. Version ID just a number. It is
   * different from the entire resource name.
   *
   * @param authConfig the gcp auth config object
   * @return the crypto key version ID. Ex: "2"
   */
  public String getPrimaryKeyVersionId(ObjectNode authConfig) {
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
    LocationName locationName = LocationName.of(projectId, locationId);
    return locationName;
  }

  public CryptoKeyVersion getPrimaryKeyVersion(ObjectNode authConfig, String cryptoKeyRN) {
    KeyManagementServiceClient client = getKMSClient(authConfig);
    CryptoKey cryptoKey = client.getCryptoKey(cryptoKeyRN);
    return cryptoKey.getPrimary();
  }

  /**
   * Verifies if a key ring resource name exists for the client created with the config object.
   * First lists all of them, then checks for a match.
   *
   * @param authConfig the gcp auth config object
   * @param keyRingRN the key ring resource name
   * @return true if key ring exists, else false.
   */
  public boolean checkKeyRingExists(ObjectNode authConfig, String keyRingRN) {
    LocationName locationName = getLocationName(authConfig);
    KeyManagementServiceClient client = getKMSClient(authConfig);
    ListKeyRingsPagedResponse response = client.listKeyRings(locationName);
    for (KeyRing keyRing : response.iterateAll()) {
      if (keyRing.getName().equals(keyRingRN)) {
        client.close();
        return true;
      }
    }
    client.close();
    return false;
  }

  /**
   * Verifies if a crypto key resource name exists for the client created with the config object.
   * First lists all of them, then checks for a match.
   *
   * @param authConfig the gcp auth config object
   * @param cryptoKeyRN the crypto key resource name
   * @return true if crypto key exists, else false.
   */
  public boolean checkCryptoKeyExists(ObjectNode authConfig, String cryptoKeyRN) {
    String keyRingRN = getKeyRingRN(authConfig);
    if (!checkKeyRingExists(authConfig, keyRingRN)) {
      log.info("Key ring doesn't exist, while checking for crypto key");
      return false;
    }
    KeyManagementServiceClient client = getKMSClient(authConfig);
    ListCryptoKeysPagedResponse response = client.listCryptoKeys(keyRingRN);
    for (CryptoKey cryptoKey : response.iterateAll()) {
      if (cryptoKey.getName().equals(cryptoKeyRN)) {
        client.close();
        return true;
      }
    }
    client.close();
    return false;
  }

  /**
   * Validates the given crypto key settings. Checks if it exists, has a rotation period, verifies
   * the purpose, and if it is enabled.
   *
   * @param authConfig the gcp auth config object
   * @param cryptoKeyRN the crypto key resource name
   * @return true if all the settings of the given crypto key are valid, else false
   */
  public boolean validateCryptoKeySettings(ObjectNode authConfig, String cryptoKeyRN) {
    if (!checkCryptoKeyExists(authConfig, cryptoKeyRN)) {
      log.info("Crypto key doesn't exist while validating crypto key settings.");
      return false;
    }
    CryptoKey cryptoKey = getCryptoKey(authConfig);
    if (cryptoKey.hasRotationPeriod()) {
      log.info("Crypto key has a rotation period. Please set it to manual rotation (never).");
      return false;
    }
    if (cryptoKey.getPurpose() != CryptoKeyPurpose.ENCRYPT_DECRYPT) {
      log.info("Crypto key purpose is not 'ENCRYPT_DECRYPT'. Please set correctly.");
      return false;
    }
    CryptoKeyVersion cryptoKeyVersion = cryptoKey.getPrimary();
    if (cryptoKeyVersion.getState() != CryptoKeyVersionState.ENABLED) {
      log.info("Primary crypto key version is not enabled. Please enable it.");
      return false;
    }
    return true;
  }

  /**
   * Creates a new key ring with the given ID. Must not already exist.
   *
   * @param authConfig the gcp auth config object
   * @param keyRingId the key ring ID to be created
   * @return the newly created key ring object
   */
  public KeyRing createKeyRing(ObjectNode authConfig) {
    KeyManagementServiceClient client = getKMSClient(authConfig);
    // Build the key ring to create.
    KeyRing keyRing = KeyRing.newBuilder().build();
    // Create the key ring.
    LocationName locationName = getLocationName(authConfig);
    String keyRingId = getConfigKeyRingId(authConfig);
    KeyRing createdKeyRing = client.createKeyRing(locationName, keyRingId, keyRing);
    client.close();
    return createdKeyRing;
  }

  /**
   * Generates a random sequence of bytes using the Google KMS API. This is used as the universe
   * key. Will be encrypted by using the crypto key created on GCP KMS and stored locally.
   *
   * @param authConfig the gcp auth config object
   * @param numBytes the number of bytes to be generated
   * @return the randomly generated byte array
   */
  public byte[] generateRandomBytes(ObjectNode authConfig, int numBytes) {
    LocationName locationName = getLocationName(authConfig);
    KeyManagementServiceClient client = getKMSClient(authConfig);
    GenerateRandomBytesResponse response =
        client.generateRandomBytes(locationName.toString(), numBytes, ProtectionLevel.HSM);
    client.close();
    return response.getData().toByteArray();
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
  public CryptoKey createCryptoKey(ObjectNode authConfig, String keyRingRN, String cryptoKeyId) {
    KeyManagementServiceClient client = getKMSClient(authConfig);
    KeyRingName keyRingName = null;

    // Check the key ring resource name syntax.
    if (KeyRingName.isParsableFrom(keyRingRN)) {
      keyRingName = KeyRingName.parse(keyRingRN);
    } else {
      log.info("Could not parse Key Ring Name from key ring resource name: " + keyRingRN);
      return null;
    }

    // Build the symmetric key with custom parameters.
    CryptoKey.Builder keyBuilder = CryptoKey.newBuilder();
    keyBuilder.setPurpose(CryptoKeyPurpose.ENCRYPT_DECRYPT);

    // Set the protection level to either HSM or default SOFTWARE
    CryptoKeyVersionTemplate.Builder cryptoKeyVersionTemplateBuilder =
        CryptoKeyVersionTemplate.newBuilder();
    if (authConfig.has(PROTECTION_LEVEL_FIELDNAME)
        && authConfig.path(PROTECTION_LEVEL_FIELDNAME).asText() == "HSM") {
      cryptoKeyVersionTemplateBuilder.setProtectionLevel(ProtectionLevel.HSM);
    }
    // Use default google symmetric encryption algorithm
    cryptoKeyVersionTemplateBuilder.setAlgorithm(
        CryptoKeyVersionAlgorithm.GOOGLE_SYMMETRIC_ENCRYPTION);
    keyBuilder.setVersionTemplate(cryptoKeyVersionTemplateBuilder);

    // Remove automatic rotation
    keyBuilder.clearRotationPeriod();
    keyBuilder.clearNextRotationTime();
    CryptoKey key = keyBuilder.build();

    // Create the key
    CryptoKey createdKey = client.createCryptoKey(keyRingName, cryptoKeyId, key);
    client.close();
    return createdKey;
  }

  /**
   * Create a key ring with a given ID if it doesn't already exist. Does nothing if already exists.
   *
   * @param authConfig the gcp auth config object
   * @param keyRingId the key ring ID to be created if not already existing
   */
  public void checkOrCreateKeyRing(ObjectNode authConfig) {
    String keyRingRN = getKeyRingRN(authConfig);
    if (checkKeyRingExists(authConfig, keyRingRN)) {
      return;
    } else {
      createKeyRing(authConfig);
    }
  }

  /**
   * Create a crypto key with a given crypto key ID in the config object if it doesn't already
   * exist. Does nothing if crypto key already exists. Key ring ID must already exist.
   *
   * @param authConfig the gcp auth config object
   */
  public void checkOrCreateCryptoKey(ObjectNode authConfig) {
    String cryptoKeyId = getConfigCryptoKeyId(authConfig);
    String keyRingRN = getKeyRingRN(authConfig);
    String cryptoKeyRN = getCryptoKeyRN(authConfig);
    if (checkCryptoKeyExists(authConfig, cryptoKeyRN)) {
      return;
    } else {
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
  public byte[] encryptBytes(ObjectNode authConfig, byte[] textToEncrypt) {
    KeyManagementServiceClient client = getKMSClient(authConfig);
    String cryptoKeyRN = getCryptoKeyRN(authConfig);
    EncryptResponse encryptResponse =
        client.encrypt(cryptoKeyRN, ByteString.copyFrom(textToEncrypt));
    client.close();
    return encryptResponse.getCiphertext().toByteArray();
  }

  /**
   * Decrypts a byte array with the crypto key. Crypto key is formed from the key ring id and crypto
   * key id stored in the config object.
   *
   * @param configUUID the config UUID
   * @param textToDecrypt the encrypted sequence of bytes, to be decrypted
   * @param config the config object
   * @return the decrypted byte array
   */
  public byte[] decryptBytes(ObjectNode authConfig, byte[] textToDecrypt) {
    KeyManagementServiceClient client = getKMSClient(authConfig);
    String cryptoKeyRN = getCryptoKeyRN(authConfig);
    DecryptResponse decryptResponse =
        client.decrypt(cryptoKeyRN, ByteString.copyFrom(textToDecrypt));
    client.close();
    return decryptResponse.getPlaintext().toByteArray();
  }

  /**
   * Verifies whether the service account has enough permissions on the keyrings, cryptokeys, etc.
   * Used in pre-flight validation check at the time of config creation.
   *
   * @param authConfig the config object with all details (credentials, location ID, etc.)
   * @return true if it has enough permission, else false
   */
  public boolean testAllPermissions(ObjectNode authConfig) {
    String projectId = getConfigProjectId(authConfig);

    CloudResourceManager service = null;
    try {
      service = createCloudResourceManagerService(authConfig);
    } catch (IOException | GeneralSecurityException e) {
      log.info("Unable to initialize service: \n" + e.toString());
      return false;
    }

    List<String> permissionsList =
        Arrays.asList(
            "cloudkms.keyRings.create",
            "cloudkms.keyRings.get",
            "cloudkms.keyRings.list",
            "cloudkms.cryptoKeys.create",
            "cloudkms.cryptoKeys.get",
            "cloudkms.cryptoKeys.list",
            "cloudkms.cryptoKeyVersions.useToEncrypt",
            "cloudkms.cryptoKeyVersions.useToDecrypt",
            "cloudkms.cryptoKeyVersions.destroy",
            "cloudkms.locations.generateRandomBytes");

    TestIamPermissionsRequest requestBody =
        new TestIamPermissionsRequest().setPermissions(permissionsList);
    try {
      TestIamPermissionsResponse testIamPermissionsResponse =
          service.projects().testIamPermissions(projectId, requestBody).execute();

      if (permissionsList.size() == testIamPermissionsResponse.getPermissions().size()) {
        log.info("Verified that the user has all 10 permissions required for GCP KMS.");
        return true;
      }
    } catch (IOException e) {
      log.error("Unable to test permissions: \n" + e.toString());
      return false;
    }
    log.warn("The user / service account does not have all 10 permissions required for GCP KMS.");
    return false;
  }

  public CloudResourceManager createCloudResourceManagerService(ObjectNode authConfig)
      throws IOException, GeneralSecurityException {
    String credentials = authConfig.path(GCP_CONFIG_FIELDNAME).toString();

    GoogleCredentials credential =
        GoogleCredentials.fromStream(new ByteArrayInputStream(credentials.getBytes()))
            .createScoped(Collections.singleton(IamScopes.CLOUD_PLATFORM));

    CloudResourceManager service =
        new CloudResourceManager.Builder(
                GoogleNetHttpTransport.newTrustedTransport(),
                GsonFactory.getDefaultInstance(),
                new HttpCredentialsAdapter(credential))
            .setApplicationName("service-accounts")
            .build();
    return service;
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
            GCP_CONFIG_FIELDNAME,
            LOCATION_ID_FIELDNAME,
            KEY_RING_ID_FIELDNAME,
            CRYPTO_KEY_ID_FIELDNAME);
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
    KeyManagementServiceClient client = getKMSClient(formData);
    if (client == null) {
      log.warn("validateKMSProviderConfigFormData: Got KMS Client = null");
      throw new Exception("Invalid GCP KMS parameters");
    }
    client.close();
    // Check if custom key ring id and crypto key id is given
    if (checkFieldsExist(formData)) {
      String keyRingId = formData.path(KEY_RING_ID_FIELDNAME).asText();
      log.info("validateKMSProviderConfigFormData: Checked all required fields exist.");
      // If given a custom key ring, validate its permissions
      if (testAllPermissions(formData) == false) {
        log.info(
            "validateKMSProviderConfigFormData: Not enough permissions for key ring = "
                + keyRingId);
        throw new Exception(
            "User does not have enough permissions or unable to test for permissions");
      }
    }
  }
}

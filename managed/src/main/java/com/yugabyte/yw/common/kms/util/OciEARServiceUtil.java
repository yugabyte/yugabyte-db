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

package com.yugabyte.yw.common.kms.util;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.oracle.bmc.Region;
import com.oracle.bmc.auth.AuthenticationDetailsProvider;
import com.oracle.bmc.auth.SimpleAuthenticationDetailsProvider;
import com.oracle.bmc.keymanagement.KmsCryptoClient;
import com.oracle.bmc.keymanagement.KmsManagementClient;
import com.oracle.bmc.keymanagement.KmsVaultClient;
import com.oracle.bmc.keymanagement.model.*;
import com.oracle.bmc.keymanagement.requests.*;
import com.oracle.bmc.keymanagement.responses.*;
import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class OciEARServiceUtil {
  private static final Logger LOG = LoggerFactory.getLogger(OciEARServiceUtil.class);

  public enum OciKmsAuthConfigField {
    TENANCY_OCID("TENANCY_OCID", true, false),
    USER_OCID("USER_OCID", true, false),
    FINGERPRINT("FINGERPRINT", true, false),
    PRIVATE_KEY("PRIVATE_KEY", true, false),
    OCI_COMPARTMENT_OCID("OCI_COMPARTMENT_OCID", true, false),
    OCI_VAULT_OCID("OCI_VAULT_OCID", false, false),
    OCI_REGION("OCI_REGION", false, true),
    OCI_KEY_NAME("OCI_KEY_NAME", false, true),
    OCI_KEY_OCID("OCI_KEY_OCID", false, false);

    public final String fieldName;
    public final boolean isEditable;
    public final boolean isMetadata;

    OciKmsAuthConfigField(String fieldName, boolean isEditable, boolean isMetadata) {
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

  public SimpleAuthenticationDetailsProvider getCredentials(ObjectNode authConfig) {
    String tenancyOcid = authConfig.path(OciKmsAuthConfigField.TENANCY_OCID.fieldName).asText();
    String userOcid = authConfig.path(OciKmsAuthConfigField.USER_OCID.fieldName).asText();
    String fingerprint = authConfig.path(OciKmsAuthConfigField.FINGERPRINT.fieldName).asText();
    String privateKey = authConfig.path(OciKmsAuthConfigField.PRIVATE_KEY.fieldName).asText();

    if (StringUtils.isAnyBlank(tenancyOcid, userOcid, fingerprint, privateKey)) {
      throw new RuntimeException("Missing required OCI credentials");
    }

    return SimpleAuthenticationDetailsProvider.builder()
        .tenantId(tenancyOcid)
        .userId(userOcid)
        .fingerprint(fingerprint)
        .privateKeySupplier(() -> new ByteArrayInputStream(privateKey.getBytes()))
        .build();
  }

  public AuthenticationDetailsProvider getAuthenticationProvider(ObjectNode authConfig) {
    return getCredentials(authConfig);
  }

  public KmsVaultClient getKmsVaultClient(UUID configUUID, ObjectNode authConfig) {
    AuthenticationDetailsProvider provider = getAuthenticationProvider(authConfig);
    if (provider == null) {
      return null;
    }

    String regionStr = authConfig.path(OciKmsAuthConfigField.OCI_REGION.fieldName).asText();
    Region region = Region.fromRegionId(regionStr);

    return KmsVaultClient.builder().region(region).build(provider);
  }

  public KmsManagementClient getKmsManagementClient(UUID configUUID, ObjectNode authConfig) {
    AuthenticationDetailsProvider provider = getAuthenticationProvider(authConfig);
    if (provider == null) {
      return null;
    }

    KmsVaultClient kmsVaultClient = getKmsVaultClient(configUUID, authConfig);
    String vaultId = authConfig.path(OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName).asText();
    Vault vault = getVaultFromId(kmsVaultClient, vaultId);
    KmsManagementClient kmsManagementClient =
        KmsManagementClient.builder().endpoint(vault.getManagementEndpoint()).build(provider);
    return kmsManagementClient;
  }

  public KmsCryptoClient getKmsCryptoClient(UUID configUUID, ObjectNode authConfig) {
    AuthenticationDetailsProvider provider = getAuthenticationProvider(authConfig);
    if (provider == null) {
      return null;
    }

    String vaultOcid = authConfig.path(OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName).asText();

    KmsVaultClient kmsVaultClient = getKmsVaultClient(configUUID, authConfig);
    Vault vault = getVaultFromId(kmsVaultClient, vaultOcid);
    String endpoint = vault.getCryptoEndpoint();

    return KmsCryptoClient.builder().endpoint(endpoint).build(provider);
  }

  public String getKeyOcid(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    return authConfig.path(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName).asText();
  }

  public CreateKeyResponse createKey(UUID configUUID, ObjectNode authConfig, String displayName) {
    KmsManagementClient client = getKmsManagementClient(configUUID, authConfig);
    if (client == null) {
      throw new RuntimeException("Failed to create KMS management client");
    }

    String compartmentOcid =
        authConfig.path(OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName).asText();

    CreateKeyDetails keyDetails =
        CreateKeyDetails.builder()
            .compartmentId(compartmentOcid)
            .displayName(displayName)
            .keyShape(
                KeyShape.builder()
                    .algorithm(KeyShape.Algorithm.Aes)
                    .length(32) // Default to 256 bits
                    .build())
            .build();

    CreateKeyRequest request = CreateKeyRequest.builder().createKeyDetails(keyDetails).build();

    return client.createKey(request);
  }

  public byte[] encryptUniverseKey(
      UUID configUUID, byte[] plainTextUniverseKey, ObjectNode authConfig) {
    KmsCryptoClient client = getKmsCryptoClient(configUUID, authConfig);
    if (client == null) {
      throw new RuntimeException("Failed to create KMS crypto client");
    }

    String keyOcid = authConfig.path(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName).asText();

    EncryptDataDetails encryptDetails =
        EncryptDataDetails.builder()
            .keyId(keyOcid)
            .plaintext(java.util.Base64.getEncoder().encodeToString(plainTextUniverseKey))
            .build();

    EncryptRequest request = EncryptRequest.builder().encryptDataDetails(encryptDetails).build();

    EncryptResponse response = client.encrypt(request);
    return java.util.Base64.getDecoder().decode(response.getEncryptedData().getCiphertext());
  }

  public byte[] decryptUniverseKey(
      UUID configUUID, byte[] encryptedUniverseKey, ObjectNode authConfig) {
    if (encryptedUniverseKey == null) {
      return null;
    }

    KmsCryptoClient client = getKmsCryptoClient(configUUID, authConfig);
    if (client == null) {
      throw new RuntimeException("Failed to create KMS crypto client");
    }

    String keyOcid = authConfig.path(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName).asText();

    DecryptDataDetails decryptDetails =
        DecryptDataDetails.builder()
            .keyId(keyOcid)
            .ciphertext(java.util.Base64.getEncoder().encodeToString(encryptedUniverseKey))
            .build();

    DecryptRequest request = DecryptRequest.builder().decryptDataDetails(decryptDetails).build();

    DecryptResponse response = client.decrypt(request);
    return java.util.Base64.getDecoder().decode(response.getDecryptedData().getPlaintext());
  }

  public void testWrapAndUnwrapKey(UUID configUUID, ObjectNode authConfig) {
    byte[] plain = new byte[32];
    new java.util.Random().nextBytes(plain);

    byte[] encrypted = encryptUniverseKey(configUUID, plain, authConfig);
    byte[] decrypted = decryptUniverseKey(configUUID, encrypted, authConfig);

    if (!java.util.Arrays.equals(plain, decrypted)) {
      throw new RuntimeException("OCI KMS wrap/unwrap test failed (decrypt != encrypt input).");
    }
  }

  public boolean checkKeyExists(ObjectNode authConfig, String keyOcid) {
    if (StringUtils.isBlank(keyOcid)) {
      return false;
    }

    try {
      KmsManagementClient client = getKmsManagementClient(null, authConfig);
      if (client == null) {
        return false;
      }

      GetKeyRequest request = GetKeyRequest.builder().keyId(keyOcid).build();

      GetKeyResponse response = client.getKey(request);
      return response.getKey() != null;
    } catch (Exception e) {
      LOG.warn("Error checking if OCI key exists: " + keyOcid, e);
      return false;
    }
  }

  // Add method to validate key settings
  public boolean validateKeySettings(ObjectNode authConfig, String keyOcid) {
    try {
      KmsManagementClient client = getKmsManagementClient(null, authConfig);
      if (client == null) {
        return false;
      }

      GetKeyRequest request = GetKeyRequest.builder().keyId(keyOcid).build();

      GetKeyResponse response = client.getKey(request);
      Key key = response.getKey();

      // Validate key is in correct state and has correct algorithm
      if (key.getLifecycleState() != Key.LifecycleState.Enabled) {
        LOG.warn("OCI key {} is not enabled state", keyOcid);
        return false;
      }

      // Check if key algorithm is AES (required for encryption)
      if (key.getKeyShape().getAlgorithm() != KeyShape.Algorithm.Aes) {
        LOG.warn("OCI key {} does not use AES algorithm", keyOcid);
        return false;
      }

      return true;
    } catch (Exception e) {
      LOG.error("Error validating OCI key settings for key: " + keyOcid, e);
      return false;
    }
  }

  public String getKeyOcidByName(UUID configUUID, ObjectNode authConfig, String targetName) {
    if (StringUtils.isBlank(targetName)) {
      return null;
    }

    String compartmentId =
        authConfig.path(OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName).asText();
    if (StringUtils.isBlank(compartmentId)) {
      throw new RuntimeException("OCI_COMPARTMENT_OCID is required to lookup key by name.");
    }

    KmsManagementClient kmsManagementClient = getKmsManagementClient(configUUID, authConfig);
    if (kmsManagementClient == null) {
      throw new RuntimeException("Failed to create OCI KMS management client.");
    }

    ListKeysRequest request = ListKeysRequest.builder().compartmentId(compartmentId).build();

    List<String> matches = new ArrayList<>();
    for (KeySummary key : kmsManagementClient.getPaginators().listKeysRecordIterator(request)) {
      if (targetName.equals(key.getDisplayName())) {
        matches.add(key.getId());
      }
    }

    if (matches.isEmpty()) {
      return null;
    }
    if (matches.size() > 1) {
      throw new RuntimeException(
          String.format(
              "Multiple OCI keys found with displayName '%s' in compartment '%s'. Please make it"
                  + " unique.",
              targetName, compartmentId));
    }
    return matches.get(0);
  }

  public byte[] generateDataEncryptionKey(UUID configUUID, ObjectNode authConfig, String keyOcid) {
    KmsCryptoClient client = getKmsCryptoClient(configUUID, authConfig);
    if (client == null) {
      throw new RuntimeException("Failed to create KMS crypto client");
    }

    GenerateKeyDetails details =
        GenerateKeyDetails.builder()
            .keyId(keyOcid)
            .keyShape(KeyShape.builder().algorithm(KeyShape.Algorithm.Aes).length(32).build())
            .includePlaintextKey(false)
            .build();

    GenerateDataEncryptionKeyRequest request =
        GenerateDataEncryptionKeyRequest.builder().generateKeyDetails(details).build();

    GenerateDataEncryptionKeyResponse response = client.generateDataEncryptionKey(request);
    return java.util.Base64.getDecoder().decode(response.getGeneratedKey().getCiphertext());
  }

  public Vault getVaultFromId(KmsVaultClient kmsVaultClient, String vaultId) {
    GetVaultRequest getVaultRequest = GetVaultRequest.builder().vaultId(vaultId).build();
    GetVaultResponse response = kmsVaultClient.getVault(getVaultRequest);

    return response.getVault();
  }

  public String getSafeText(ObjectNode node, String fieldName) {
    if (node.hasNonNull(fieldName)) {
      return node.get(fieldName).asText();
    }
    return null;
  }

  public void validateKMSProviderConfigFormData(ObjectNode formData) {
    String compartmentOcid =
        getSafeText(formData, OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName);
    String vaultOcid = getSafeText(formData, OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName);
    String region = getSafeText(formData, OciKmsAuthConfigField.OCI_REGION.fieldName);
    if (StringUtils.isBlank(compartmentOcid)) {
      throw new RuntimeException("OCI_COMPARTMENT_OCID is required");
    }
    if (StringUtils.isBlank(vaultOcid)) {
      throw new RuntimeException("OCI_VAULT_OCID is required");
    }
    if (StringUtils.isBlank(region)) {
      throw new RuntimeException("OCI_REGION is required");
    }

    // Test authentication
    AuthenticationDetailsProvider provider = getAuthenticationProvider(formData);
    if (provider == null) {
      throw new RuntimeException(
          "Failed to authenticate with OCI. Please check config file and credentials.");
    }

    // If key OCID is provided, validate it exists and is accessible
    String keyOcid = getSafeText(formData, OciKmsAuthConfigField.OCI_KEY_OCID.fieldName);
    if (StringUtils.isNotBlank(keyOcid)) {
      if (!checkKeyExists(formData, keyOcid)) {
        throw new RuntimeException(
            "OCI key with OCID " + keyOcid + " does not exist or is not accessible");
      }
      if (!validateKeySettings(formData, keyOcid)) {
        throw new RuntimeException("OCI key with OCID " + keyOcid + " has invalid settings");
      }
    }
  }
}

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
import com.oracle.bmc.identity.IdentityClient;
import com.oracle.bmc.identity.requests.ListRegionsRequest;
import com.oracle.bmc.keymanagement.KmsCryptoClient;
import com.oracle.bmc.keymanagement.KmsManagementClient;
import com.oracle.bmc.keymanagement.KmsVaultClient;
import com.oracle.bmc.keymanagement.model.*;
import com.oracle.bmc.keymanagement.requests.*;
import com.oracle.bmc.keymanagement.responses.*;
import com.oracle.bmc.model.BmcException;
import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
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
    return resolveKeyOcid(configUUID, authConfig);
  }

  /**
   * Resolves the OCI key OCID from the auth config, treating OCI_KEY_NAME as the single source of
   * truth. If a cached OCI_KEY_OCID is already present it is returned as-is. Otherwise the OCID is
   * looked up from OCI_KEY_NAME via {@link #getKeyOcidByName}, the key is created if it does not
   * yet exist, and the resolved OCID is cached back into the supplied auth config.
   */
  public String resolveKeyOcid(UUID configUUID, ObjectNode authConfig) {
    String keyOcid = getSafeText(authConfig, OciKmsAuthConfigField.OCI_KEY_OCID.fieldName);
    if (StringUtils.isNotBlank(keyOcid)) {
      return keyOcid;
    }

    String keyName = getSafeText(authConfig, OciKmsAuthConfigField.OCI_KEY_NAME.fieldName);
    if (StringUtils.isBlank(keyName)) {
      throw new RuntimeException("OCI_KEY_NAME is required to resolve the OCI KMS key.");
    }

    String foundOcid = getKeyOcidByName(configUUID, authConfig, keyName);
    if (StringUtils.isBlank(foundOcid)) {
      CreateKeyResponse resp = createKey(configUUID, authConfig, keyName);
      foundOcid = resp.getKey().getId();
    }
    authConfig.put(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName, foundOcid);
    return foundOcid;
  }

  public CreateKeyResponse createKey(UUID configUUID, ObjectNode authConfig, String displayName) {
    KmsManagementClient client = getKmsManagementClient(configUUID, authConfig);
    if (client == null) {
      throw new RuntimeException("Failed to create KMS management client");
    }

    String compartmentId =
        authConfig.path(OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName).asText();
    if (StringUtils.isBlank(compartmentId)) {
      throw new RuntimeException("OCI_COMPARTMENT_OCID is required to lookup key by name.");
    }

    CreateKeyDetails keyDetails =
        CreateKeyDetails.builder()
            .compartmentId(compartmentId)
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

    String keyOcid = resolveKeyOcid(configUUID, authConfig);

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

    String keyOcid = resolveKeyOcid(configUUID, authConfig);

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

  /**
   * Returns true if the key exists (HTTP 200), false if it does not (HTTP 404 or blank OCID).
   *
   * <p>Throws a descriptive RuntimeException for HTTP 403 (insufficient permissions) or any other
   * unexpected API error, so callers are not silently misled. Lifecycle state and algorithm
   * validation are left to {@link #validateKeySettings}.
   */
  public boolean checkKeyExists(ObjectNode authConfig, String keyOcid) {
    if (StringUtils.isBlank(keyOcid)) {
      return false;
    }

    KmsManagementClient client = getKmsManagementClient(null, authConfig);

    try {
      GetKeyResponse response = client.getKey(GetKeyRequest.builder().keyId(keyOcid).build());
      return response.getKey() != null;
    } catch (BmcException e) {
      if (e.getStatusCode() == 404) {
        return false;
      }
      if (e.getStatusCode() == 403) {
        throw new RuntimeException(
            "Insufficient permissions to access OCI key '" + keyOcid + "'.", e);
      }
      throw new RuntimeException(
          "Error accessing OCI key '"
              + keyOcid
              + "': HTTP "
              + e.getStatusCode()
              + ": "
              + e.getMessage(),
          e);
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
    // Step 1: fail fast on blank fields not already covered by getCredentials().
    // Credential fields (TENANCY_OCID, USER_OCID, FINGERPRINT, PRIVATE_KEY) are checked inside
    // getCredentials() via StringUtils.isAnyBlank, so we only need the three below here.
    String vaultOcid = getSafeText(formData, OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName);
    String regionStr = getSafeText(formData, OciKmsAuthConfigField.OCI_REGION.fieldName);
    String compartmentId =
        getSafeText(formData, OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName);
    if (StringUtils.isBlank(vaultOcid)) throw new RuntimeException("OCI_VAULT_OCID is required");
    if (StringUtils.isBlank(regionStr)) throw new RuntimeException("OCI_REGION is required");
    if (StringUtils.isBlank(compartmentId))
      throw new RuntimeException("OCI_COMPARTMENT_OCID is required");

    // Step 2: parse region locally no network call, catches completely invalid strings early.
    Region region;
    try {
      region = Region.fromRegionId(regionStr);
    } catch (IllegalArgumentException e) {
      throw new RuntimeException(
          "Invalid OCI_REGION: '" + regionStr + "' is not a recognized OCI region identifier.", e);
    }

    // Step 3: validate credentials with a live Identity API call (listRegions is cheap and global).
    // This is the only step that can confirm tenancy/user/fingerprint/private-key are all correct.
    SimpleAuthenticationDetailsProvider authProvider = getCredentials(formData);
    try {
      validateCredentialsWithIdentityService(region, authProvider);
    } catch (BmcException e) {
      if (e.getStatusCode() == 401 || e.getStatusCode() == 403) {
        throw new RuntimeException(
            "Invalid OCI credentials. Please verify REGION, TENANCY_OCID, USER_OCID, FINGERPRINT,"
                + " and PRIVATE_KEY.",
            e);
      }
      throw new RuntimeException("OCI credential validation failed: " + e.getMessage(), e);
    } catch (Exception e) {
      if (containsIgnoreCase(e.getMessage(), "private key")
          || containsIgnoreCase(e.getMessage(), "pkcs")
          || containsIgnoreCase(e.getMessage(), "pem")) {
        throw new RuntimeException(
            "Invalid PRIVATE_KEY. Could not parse the OCI private key content.", e);
      }
      throw new RuntimeException("OCI credential validation failed: " + e.getMessage(), e);
    }

    // Step 4: validate vault credentials are confirmed valid at this point, so a failure here
    // is unambiguously a vault/region mismatch rather than an auth issue.
    KmsVaultClient kmsVaultClient = getKmsVaultClient(null, formData);
    try {
      getVaultFromId(kmsVaultClient, vaultOcid);
    } catch (BmcException e) {
      if (e.getStatusCode() == 404) {
        throw new RuntimeException(
            String.format(
                "OCI vault is not accessible. Verify that OCI_VAULT_OCID '%s' exists in region"
                    + " OCI_REGION '%s'.",
                vaultOcid, regionStr),
            e);
      }
      if (e.getStatusCode() == 403) {
        throw new RuntimeException(
            "Insufficient permissions to access OCI vault '" + vaultOcid + "'.", e);
      }
      throw new RuntimeException(
          "Failed to access OCI vault '" + vaultOcid + "': " + e.getMessage(), e);
    }

    // Step 5: validate the key by name (the single source of truth).
    // OCI_KEY_NAME is required. If a key with this display name already exists, validate its
    // settings (lifecycle state must be Enabled and algorithm must be AES). A non-existent name is
    // allowed here; the key will be created at config-create time. getKeyOcidByName throws if
    // multiple keys share the same display name.
    String keyName = getSafeText(formData, OciKmsAuthConfigField.OCI_KEY_NAME.fieldName);
    if (StringUtils.isBlank(keyName)) {
      throw new RuntimeException("OCI_KEY_NAME is required");
    }
    String keyOcid = getKeyOcidByName(null, formData, keyName);
    if (StringUtils.isNotBlank(keyOcid) && !validateKeySettings(formData, keyOcid)) {
      throw new RuntimeException(
          "OCI key '"
              + keyName
              + "' has invalid settings: it must be in Enabled state and use AES algorithm.");
    }
  }

  /**
   * Makes a cheap {@code listRegions} call to confirm that the supplied auth provider can
   * successfully authenticate against OCI. Extracted as a protected method so tests can stub it.
   */
  protected void validateCredentialsWithIdentityService(
      Region region, SimpleAuthenticationDetailsProvider authProvider) {
    try (IdentityClient identityClient =
        IdentityClient.builder().region(region).build(authProvider)) {
      identityClient.listRegions(ListRegionsRequest.builder().build());
    }
  }

  private boolean containsIgnoreCase(String source, String target) {
    if (source == null || target == null) {
      return false;
    }
    return source.toLowerCase(Locale.ROOT).contains(target.toLowerCase(Locale.ROOT));
  }
}

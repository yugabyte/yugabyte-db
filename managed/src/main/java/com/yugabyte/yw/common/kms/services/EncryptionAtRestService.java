/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

/**
 * Should be implemented for each EncryptionAtRestService impl as an enum of supported
 * encryption algorithms
 */
interface SupportedAlgorithmInterface {
    List<Integer> getKeySizes();
    String name();
}

/**
 * An interface to be implemented for each encryption key provider service that YugaByte supports
 */
public abstract class EncryptionAtRestService<T extends SupportedAlgorithmInterface> {
    /**
     * Logger for service
     */
    protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestService.class);

    /**
     * A human-friendly representation of a given instance's service provider
     */
    protected KeyProvider keyProvider;

    /**
     * A helper to retrieve the supported encryption algorithms for a given key provider
     *
     * @return an array of SupportedAlgorithmInterface implementation enums
     */
    protected abstract T[] getSupportedAlgorithms();

    /**
     * A method that provides middleware mediation on the requested encryption algorithm to be used
     *
     * @param algorithm is a String representation of a SupportedAlgorithmInterface implementation
     * @return either a SupportedAlgorithmInterface implementation of the inputted algorithm,
     * or null if no matching algorithm is found
     */
    private T validateEncryptionAlgorithm(String algorithm) {
        return (T) Arrays.stream(getSupportedAlgorithms())
                .filter(algo -> ((T)algo).name().equals(algorithm))
                .findFirst()
                .orElse(null);
    }

    /**
     * A method that provides middleware mediation on the requested encryption key size
     *
     * @param keySize is the desired size of the encryption key to be created/used in bits
     * @param algorithm is the encryption algorithm that is to be used in conjunction with the
     *                  encryption key
     * @return true if the keySize is supported, and false otherwise
     */
    private boolean validateKeySize(int keySize, T algorithm) {
        return algorithm.getKeySizes()
                .stream()
                .anyMatch(supportedKeySize -> supportedKeySize.intValue() == keySize);
    }

    /**
     * A method that attempts to remotely create an encryption key with an encryption
     * service provider
     *
     * @param universeUUID is the universe that the encryption key is being created for
     * @param customerUUID is the customer that the encryption key is being created for
     * @param config containing provider-specific creation parameters
     * @return an encryption key Id that can be used to retrieve the key if everything succeeds
     * during creation
     */
    protected abstract byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    );

    /**
     * A method for creating an encryption key, and then retrieving it's value for use
     *
     * @param universeUUID is the universe the key should be created for
     * @param customerUUID is the customer the key should be created for
     * @param config contains any needed parameters for creating an encryption key
     * @return the encryption key value
     */
    public byte[] createKey(UUID universeUUID, UUID customerUUID, Map<String, String> config) {
        byte[] key = null;
        try {
            final byte[] existingEncryptionKey = retrieveKey(customerUUID, universeUUID, config);
            if (existingEncryptionKey != null && existingEncryptionKey.length > 0) {
                final String errMsg = String.format(
                        "Encryption key for customer %s and universe %s already exists" +
                                " with provider %s",
                        customerUUID.toString(),
                        universeUUID.toString(),
                        this.keyProvider.name()
                );
                LOG.error(errMsg);
                throw new IllegalArgumentException(errMsg);
            }
            final byte[] ref = createKeyWithService(universeUUID, customerUUID, config);
            if (ref == null || ref.length == 0) {
                final String errMsg = "createKeyWithService returned empty key ref";
                LOG.error(errMsg);
                throw new RuntimeException(errMsg);
            }
            key = retrieveKey(customerUUID, universeUUID, ref, config);
        } catch (Exception e) {
            LOG.error("Error occured attempting to create encryption key", e);
        }
        return key;
    }

    /**
     * This method can be used to rotate the universe key associated with a customer & universe
     *
     * @param customerUUID the customer the key is for
     * @param universeUUID the universe the key is for
     * @param config the parameters of the new key to be rotated to (i.e. algorithm/keySize)
     * @return a newly created universe key still associated under the same name/alias as the
     * previous key that has now been rotated
     */
    public byte[] rotateKey(UUID customerUUID, UUID universeUUID, Map<String, String> config) {
        final byte[] ref = rotateKeyWithService(customerUUID, universeUUID, config);
        if (ref == null || ref.length == 0) {
            final String errMsg = "rotateKeyWithService returned empty key ref";
            LOG.error(errMsg);
            throw new RuntimeException(errMsg);
        }
        addKeyRef(customerUUID, universeUUID, ref);
        return retrieveKey(customerUUID, universeUUID, config);
    }

    /**
     * This method should be implemented for each specific integration to rotate a universe
     * key with a new value
     *
     * @param customerUUID the customer the key is for
     * @param universeUUID the universe the key is for
     * @param config the parameters of the new key to be rotated to (i.e. algorithm/keySize)
     * @return a newly created universe key associated under the same name/alias as the
     * previous key that has now been rotated
     */
    protected abstract byte[] rotateKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    );

    /**
     * Base Encryption Service Constructor
     *
     * @param apiHelper apiHelper is the apiHelper to help out with API requests
     * @param keyProvider is the String representation of the KeyProvider enum that this interface
     *                    has been instantiated for
     */
    protected EncryptionAtRestService(KeyProvider keyProvider) {
        this.keyProvider = keyProvider;
    }

    /**
     * Validate parameters inputted to a createEncryptionKey request
     *
     * @param algorithm is a string representation of a SupportedAlgorithmInterface impl enum
     * @param keySize is the size of the encryption key (in bits)
     * @return true if the parameters are valid, false otherwise and errors
     */
    protected ObjectNode validateEncryptionKeyParams(String algorithm, int keySize) {
        final T encryptionAlgorithm = validateEncryptionAlgorithm(algorithm);
        ObjectNode result = Json.newObject().put("result", false);
        if (encryptionAlgorithm == null) {
            final String errMsg = String.format(
                    "Requested encryption algorithm \"%s\" is not currently supported",
                    algorithm
            );
            LOG.error(errMsg);
            result.put("errors", errMsg);
        } else if (!validateKeySize(keySize, encryptionAlgorithm)) {
            final String errMsg = String.format(
                    "Requested key size %d bits is not supported by requested encryption " +
                            "algorithm \"%s\"",
                    keySize,
                    algorithm
            );
            LOG.error(errMsg);
            result.put("errors", errMsg);
        } else {
            result.put("result", true);
        }
        return result;
    }

    /**
     * This method attempts to create a KmsConfig for the specified customer to be used to
     * store authentication information for communicating with a third party encryption service
     * provider
     *
     * @param customerUUID is the customer UUID that the auth config will be created for
     * @param config is the third party service provider authentication configuration params
     */
    public KmsConfig createAuthConfig(UUID customerUUID, ObjectNode config) {
        final KmsConfig existingConfig = getKMSConfig(customerUUID);
        if (existingConfig != null) return null;
        final ObjectNode encryptedConfig = EncryptionAtRestUtil.maskConfigData(
                customerUUID,
                config,
                this.keyProvider
        );
        return KmsConfig.createKMSConfig(customerUUID, this.keyProvider, encryptedConfig);
    }

    public ObjectNode getAuthConfig(UUID customerUUID) {
        return EncryptionAtRestUtil.getAuthConfig(customerUUID, this.keyProvider);
    }

    /**
     * A method to retrieve an encrypted at rest universe's key ref rotation history
     *
     * @param customerUUID the customer the KMS configuration belongs to
     * @param universeUUID the universe that is encrypted at rest
     * @return an array of key refs representing each key that has at one time been associated to
     * the universe
     */
    public List<KmsHistory> getKeyRotationHistory(UUID customerUUID, UUID universeUUID) {
        KmsConfig config = getKMSConfig(customerUUID);
        List<KmsHistory> rotationHistory = KmsHistory.getAllTargetKeyRefs(
                config.configUUID,
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY
        );
        if (rotationHistory == null) {
            LOG.warn(String.format(
                    "No rotation history exists for universe %s",
                    universeUUID.toString()
            ));
            rotationHistory = new ArrayList<KmsHistory>();
        }
        return rotationHistory;
    }

    /**
     * This method clears out the keyRef history for the given universe
     *
     * @param customerUUID the customer that the KMS configuration for the universe is
     *                     associated to
     * @param universeUUID the universe that will have it's key ref history cleared
     */
    public void removeKeyRotationHistory(UUID customerUUID, UUID universeUUID) {
        KmsConfig config = getKMSConfig(customerUUID);
        // Remove key ref history for the universe
        if (config != null) KmsHistory.deleteAllTargetKeyRefs(
                config.configUUID,
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY
        );
        // Remove in-memory key ref -> key val cache entry, if it exists
        EncryptionAtRestUtil.removeUniverseKeyCacheEntry(universeUUID);
    }

    public void removeKeyRef(UUID customerUUID, UUID universeUUID) {
        KmsConfig config;
        try {
            config = getKMSConfig(customerUUID);
            KmsHistory currentRef = KmsHistory.getCurrentKeyRef(
                    config.configUUID,
                    universeUUID,
                    KmsHistoryId.TargetType.UNIVERSE_KEY
            );
            if (currentRef != null) KmsHistory.deleteKeyRef(currentRef);
        } catch (Exception e) {
            String errMsg = "Could not remove key ref";
            LOG.error(errMsg);
        }
    }

    /**
     * This method is used to retrieve historically-rotated universe key data
     *
     * @param customerUUID the customer the universe was created for
     * @param universeUUID the universe that should have its key history retrieved
     * @return a keyRef object if any exists, or null if none does
     */
    public byte[] getKeyRef(UUID customerUUID, UUID universeUUID) {
        byte[] keyRef = null;
        try {
            KmsConfig config = getKMSConfig(customerUUID);
            if (config != null) {
                KmsHistory currentRef = KmsHistory.getCurrentKeyRef(
                        config.configUUID,
                        universeUUID,
                        KmsHistoryId.TargetType.UNIVERSE_KEY
                );
                if (currentRef != null) keyRef = Base64.getDecoder().decode(currentRef.keyRef);
            }
        } catch (Exception e) {
            final String errMsg = "Could not get key ref";
            LOG.error(errMsg, e);
        }
        return keyRef;
    }

    /**
     * This method adds a newly created key (usually from key rotation) to the key ref
     * history
     *
     * @param customerUUID is the customer that has the universe associated to them
     * @param universeUUID is the universe the key history is for
     * @param ref is some KMS provider - specific key reference
     */
    public void addKeyRef(UUID customerUUID, UUID universeUUID, byte[] ref) {
        KmsConfig config = getKMSConfig(customerUUID);
        KmsHistory.createKmsHistory(
                config.configUUID,
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY,
                Base64.getEncoder().encodeToString(ref)
        );
    }

    /**
     * This method deleted a KMS configuration for the instantiated service type
     *
     * @param customerUUID is the customer that the configuration should be deleted for
     */
    public void deleteKMSConfig(UUID customerUUID) {
        if (!configInUse(customerUUID)) {
            final KmsConfig config = getKMSConfig(customerUUID);
            if (config != null) config.delete();
        } else throw new IllegalArgumentException(String.format(
                "Cannot delete %s KMS Configuration for customer %s since at least 1 universe" +
                        " exists using encryptionAtRest with this KMS Configuration",
                this.keyProvider.name(),
                customerUUID.toString()
        ));
    }

    /**
     * This method attempts to retrieve a KmsConfig containing authentication
     * information for the given encryption service provider
     *
     * @param customerUUID the UUID of the customer that this config is for
     * @return a KmsConfig instance containing authentication JSON as a data field
     */
    public KmsConfig getKMSConfig(UUID customerUUID) {
        return KmsConfig.getKMSConfig(customerUUID, this.keyProvider);
    }

    /**
     * This method will query the third party key provider and try and find an encryption key
     * with a name matching the inputted universeUUID
     *
     * @param customerUUID is the customer that the encryption keyt should be recovered for
     * @param universeUUID is the UUID of the universe that we want to recover the encryption
     *                     key for
     * @return the value of the encryption key if a matching key is found
     */
    public byte[] retrieveKey(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    ) {
        byte[] keyVal = null;
        if (keyRef == null) {
            String errMsg = String.format(
                    "Retrieve key could not find a key ref for universe %s...",
                    universeUUID.toString()
            );
            LOG.warn(errMsg);
            return null;
        }
        // Attempt to retrieve cached entry
        keyVal = EncryptionAtRestUtil.getUniverseKeyCacheEntry(universeUUID, keyRef);
        // Retrieve through KMS provider if no cache entry exists
        if (keyVal == null) {
            LOG.info("Universe key cache entry empty. Retrieving key from service");
            keyVal = retrieveKeyWithService(customerUUID, universeUUID, keyRef, config);
        }
        return keyVal;
    }

    public byte[] retrieveKey(UUID customerUUID, UUID universeUUID, Map<String, String> config) {
        byte[] keyRef = getKeyRef(customerUUID, universeUUID);
        return retrieveKey(customerUUID, universeUUID, keyRef, config);

    }

    public byte[] retrieveKey(UUID customerUUID, UUID universeUUID) {
        return retrieveKey(customerUUID, universeUUID, null);
    }

    protected abstract byte[] retrieveKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    );

    /**
     * This method checks whether any key ref rotation history entries exist for any universe
     * in the KMS config
     *
     * @param customerUUID the customer that the KMS config is associated to
     * @return true if the config is associated to any universes, false otherwise
     */
    public boolean configInUse(UUID customerUUID) {
        boolean result = false;
        final KmsConfig config = getKMSConfig(customerUUID);
        if (config != null) result = KmsHistory
                .configHasHistory(config.configUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
        return result;
    }

    public static KeyProvider[] getKeyProviders() {
        return KeyProvider.values();
    }
}

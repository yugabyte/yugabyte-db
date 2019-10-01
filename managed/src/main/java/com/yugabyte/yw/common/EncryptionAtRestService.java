// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.CustomerConfig;
import java.lang.InstantiationException;
import java.lang.reflect.Constructor;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
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
     * To be used to make requests against a KMS provider
     */
    protected ApiHelper apiHelper;

    /**
     * A human-friendly representation of a given instance's service provider
     */
    protected String keyProvider;

    /**
     * A list of third party encryption key providers that YB currently supports and the
     * corresponding service impl and any already instantiated classes
     * (such that each impl is a singleton)
     */
    public enum KeyProvider {
        AWS(AwsEARService.class),
        SMARTKEY(SmartKeyEARService.class);

        private Class providerService;

        private EncryptionAtRestService instance;

        public Class getProviderService() {
            return this.providerService;
        }

        public EncryptionAtRestService getServiceInstance() { return this.instance; }

        public void setServiceInstance(EncryptionAtRestService instance) {
            this.instance = instance;
        }

        private KeyProvider(Class providerService) {
            this.providerService = providerService;
            this.instance = null;
        }
    }

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
    protected abstract String createEncryptionKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    );

    /**
     * A method that attempts to remotely retrieve a created encryption key from
     * the key provider
     *
     * @param kId is the third-party pkId for the remotely-created object
     * @param customerUUID is the customer that we should attempt to retrieve the encryption
     *                     key for
     * @param config contains provider and key specific parameters to help locate the key
     * @return the value of the encryption key
     */
    protected abstract byte[] getEncryptionKeyWithService(
            String kId,
            UUID customerUUID,
            UUID universeUUID,
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
    public byte[] createAndRetrieveEncryptionKey(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        try {
            final String algorithm = config.get("algorithm");
            final int keySize = Integer.parseInt(config.get("key_size"));
            final byte[] existingEncryptionKey = recoverEncryptionKeyWithService(
                    customerUUID,
                    universeUUID,
                    config
            );
            if (existingEncryptionKey != null && existingEncryptionKey.length > 0) {
                final String errMsg = String.format(
                        "Encryption key for customer %s and universe %s already exists" +
                                " with provider %s",
                        customerUUID.toString(),
                        universeUUID.toString(),
                        this.keyProvider
                );
                LOG.error(errMsg);
                throw new IllegalArgumentException(errMsg);
            }
            final ObjectNode validateResult = validateEncryptionKeyParams(algorithm, keySize);
            if (!validateResult.get("result").asBoolean()) {
                final String errMsg = String.format(
                        "Invalid encryption key parameters detected for create operation in " +
                                "universe %s: %s",
                        universeUUID,
                        validateResult.get("errors").asText()
                );
                LOG.error(errMsg);
                throw new IllegalArgumentException(errMsg);
            }
            final String kId = createEncryptionKeyWithService(
                    universeUUID,
                    customerUUID,
                    config
            );
            if (kId == null || kId.isEmpty()) {
                final String errMsg = String.format(
                        "Error remotely creating encryption key through %s for universe %s",
                        this.keyProvider,
                        universeUUID
                );
                LOG.error(errMsg);
                throw new RuntimeException(errMsg);
            }
            return getEncryptionKeyWithService(kId, customerUUID, universeUUID, config);
        } catch (Exception e) {
            LOG.error("Error occured attempting to create encryption key", e);
            return null;
        }
    }

    /**
     * Base Encryption Service Constructor
     *
     * @param apiHelper apiHelper is the apiHelper to help out with API requests
     * @param keyProvider is the String representation of the KeyProvider enum that this interface
     *                    has been instantiated for
     */
    protected EncryptionAtRestService(ApiHelper apiHelper, String keyProvider) {
        this.apiHelper = apiHelper;
        this.keyProvider = keyProvider;
    }

    /**
     * Validate parameters inputted to a createEncryptionKey request
     *
     * @param algorithm is a string representation of a SupportedAlgorithmInterface impl enum
     * @param keySize is the size of the encryption key (in bits)
     * @return true if the parameters are valid, false otherwise and errors
     */
    private ObjectNode validateEncryptionKeyParams(String algorithm, int keySize) {
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
                            "algorithm %s",
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
     * A method that processes config data depending on the key provider to encrypt any sensitive
     * data before storing
     *
     * Uses "stronger" password-based encryption
     *
     * @param customerUUID is the customer that the KMS config is associated to
     * @param config is the data field of the KMS config that should be encrypted
     * @return an encrypted representation of config
     */
    protected ObjectNode encryptConfigData(UUID customerUUID, ObjectNode config) {
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String salt = Util.generateSalt(customerUUID, this.keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
            return Json.newObject().put("encrypted_config", encryptedConfig);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not encrypt %s KMS configuration for customer %s",
                    this.keyProvider,
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    /**
     * A method that processes config data depending on the key provider to decrypt any sensitive
     * data that had been encrypted before storing
     *
     * Uses "stronger" password-based encryption
     *
     * @param customerUUID is the customer that the KMS config is associated to
     * @param config is the data field of the KMS config that should be decrypted
     * @return a decrypted representation of config
     */
    protected ObjectNode decryptConfigData(UUID customerUUID, ObjectNode config) {
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String encryptedConfig = config.get("encrypted_config").asText();
            final String salt = Util.generateSalt(customerUUID, this.keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String decryptedConfig = encryptor.decrypt(encryptedConfig);
            return mapper.readValue(decryptedConfig, ObjectNode.class);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not decrypt %s KMS configuration for customer %s",
                    this.keyProvider,
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    /**
     * This method attempts to create a CustomerConfig for the specified customer to be used to
     * store authentication information for communicating with a third party encryption service
     * provider
     *
     * @param customerUUID is the customer UUID that the auth config will be created for
     * @param config is the third party service provider authentication configuration params
     */
    public CustomerConfig createAuthConfig(UUID customerUUID, ObjectNode config) {
        final CustomerConfig existingConfig = CustomerConfig.getKMSConfig(
                customerUUID,
                this.keyProvider
        );
        if (existingConfig != null) return null;
        final ObjectNode encryptedConfig = encryptConfigData(customerUUID, config);
        return CustomerConfig.createKMSConfig(customerUUID, this.keyProvider, encryptedConfig);
    }

    /**
     * This method attempts to retrieve CustomerConfig data representing the authentication
     * information for the given encryption service provider
     *
     * @param customerUUID the UUID of the customer that this config is for
     * @return an ObjectNode containing authentication information, or null if none exists
     */
    public ObjectNode getAuthConfig(UUID customerUUID) {
        final ObjectNode config = CustomerConfig.getKMSAuthObj(customerUUID, this.keyProvider);
        if (config == null) {
            return null;
        }
        return decryptConfigData(customerUUID, config);
    }

    /**
     * This method updates a KMS CustomerConfig entry's data field with whatever newValues contains
     *
     * @param customerUUID is the customer the configuration is associated to
     * @param newValues is a map of field name -> field value of updated data field values
     * @return a copy of the updated CustomerConfig (with data field encrypted)
     */
    public ObjectNode updateAuthConfig(UUID customerUUID, Map<String, JsonNode> newValues) {
        ObjectNode config = getAuthConfig(customerUUID);
        for (Map.Entry<String, JsonNode> newValue : newValues.entrySet()) {
            config.put(newValue.getKey(), newValue.getValue());
        }
        final ObjectNode encryptedConfig = encryptConfigData(customerUUID, config);
        return CustomerConfig.updateKMSAuthObj(
                customerUUID,
                this.keyProvider,
                encryptedConfig
        ).getData().deepCopy();
    }

    /**
     * This method deleted a KMS configuration for the instantiated service type
     *
     * @param customerUUID is the customer that the configuration should be deleted for
     */
    public void deleteKMSConfig(UUID customerUUID) {
        CustomerConfig config = getKMSConfig(customerUUID);
        if (config != null) config.delete();
    }

    /**
     * This method attempts to retrieve a CustomerConfig containing authentication
     * information for the given encryption service provider
     *
     * @param customerUUID the UUID of the customer that this config is for
     * @return a CustomerConfig instance containing authentication JSON as a data field
     */
    public CustomerConfig getKMSConfig(UUID customerUUID) {
        return CustomerConfig.getKMSConfig(customerUUID, this.keyProvider);
    }

    /**
     * This method will query the third party key provider and try and find an encryption key
     * with a name matching the inputted universeUUID
     *
     * @param customerUUID is the customer that the encryption keyt should be recovered for
     * @param universeUUID is the UUID of the universe that we want to recover the encryption
     *                     key for
     * @param config is a service-specific map of keys/values to help identify the key
     * @return the value of the encryption key if a matching key is found
     */
    public abstract byte[] recoverEncryptionKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    );

    /**
     * This method iterates through a service provider's defined public constructors, and tries to
     * find one with a signature that is matching what is expected by the service
     *
     * @param serviceClass is the service provider's implementation class
     * @return a constructor that is valid, or null if none are found
     */
    private static Constructor getConstructor(Class serviceClass) {
        Constructor serviceConstructor = null;
        Class[] parameterTypes;
        for (Constructor constructor : serviceClass.getConstructors()) {
            parameterTypes = constructor.getParameterTypes();
            if (
                    constructor.getParameterCount() == 2 &&
                    ApiHelper.class.isAssignableFrom(parameterTypes[0]) &&
                    String.class.isAssignableFrom(parameterTypes[1])
            ) {
                serviceConstructor = constructor;
                break;
            }
        }
        return serviceConstructor;
    }

    public static EncryptionAtRestService getServiceInstance(
            ApiHelper apiHelper,
            String keyProvider
    ) { return getServiceInstance(apiHelper, keyProvider, false); }

    /**
     * This is the entry point into the service. All service instances should be retrieved through
     * this method. Will maintain a mapping of KeyProvider -> Instances such that there will be AT
     * MOST one instance per encryption service provider per Yugaware instance
     *
     * @param apiHelper a utility library used to make requests
     * @param keyProvider is the service provider that should have a service instance
     *                    returned for
     * @return a properly configured and instantiated service if everything succeeded,
     * null otherwise
     */
    public static EncryptionAtRestService getServiceInstance(
            ApiHelper apiHelper,
            String keyProvider,
            boolean forceNewInstance
    ) {
        KeyProvider serviceProvider = null;
        EncryptionAtRestService serviceInstance = null;
        try {
            for (KeyProvider providerImpl : KeyProvider.values()) {
                if (providerImpl.name().equals(keyProvider)) {
                    serviceProvider = providerImpl;
                    break;
                }
            }
            if (serviceProvider == null) {
                final String errMsg = String.format(
                        "Encryption service provider %s is not supported",
                        keyProvider
                );
                LOG.error(errMsg);
                throw new IllegalArgumentException(errMsg);
            }
            serviceInstance = serviceProvider.getServiceInstance();
            if (forceNewInstance || serviceInstance == null) {
                final Class serviceClass = serviceProvider.getProviderService();
                if (serviceClass == null) {
                    final String errMsg = String.format(
                            "Encryption service provider %s has not been implemented yet",
                            keyProvider
                    );
                    LOG.error(errMsg);
                    throw new IllegalArgumentException(errMsg);
                }
                final Constructor serviceConstructor = getConstructor(serviceClass);
                if (serviceConstructor == null) {
                    final String errMsg = String.format(
                            "No suitable public constructor found for service provider %s",
                            keyProvider
                    );
                    LOG.error(errMsg);
                    throw new InstantiationException(errMsg);
                }
                serviceInstance = (EncryptionAtRestService) serviceConstructor.newInstance(
                        apiHelper,
                        keyProvider
                );
                if (serviceInstance != null) serviceProvider.setServiceInstance(serviceInstance);
            }
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Error occurred attempting to retrieve encryption key service for " +
                            "key provider %s",
                    keyProvider
            );
            LOG.error(errMsg, e);
            serviceInstance = null;
        }
        return serviceInstance;
    }
}

/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.AWSKMSClientBuilder;
import com.amazonaws.services.kms.model.AliasListEntry;
import com.amazonaws.services.kms.model.CreateAliasRequest;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DecryptRequest;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextRequest;
import com.amazonaws.services.kms.model.KeyListEntry;
import com.amazonaws.services.kms.model.ListAliasesRequest;
import com.amazonaws.services.kms.model.ListAliasesResult;
import com.amazonaws.services.kms.model.ListKeysRequest;
import com.amazonaws.services.kms.model.ListKeysResult;
import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.EncryptionAtRestManager.KeyProvider;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.UUID;

enum AwsAlgorithm implements SupportedAlgorithmInterface {
    AES(Arrays.asList(128, 256));

    private List<Integer> keySizes;

    public List<Integer> getKeySizes() {
        return this.keySizes;
    }

    private AwsAlgorithm(List<Integer> keySizes) {
        this.keySizes = keySizes;
    }
}

public class AwsEARService extends EncryptionAtRestService<AwsAlgorithm> {
    /**
     * Constructor
     *
     * @param apiHelper is a library to make requests against a third party encryption key provider
     * @param keyProvider is a String representation of "SMARTKEY" (if it is valid in this context)
     */
    public AwsEARService() {
        super(KeyProvider.AWS);
    }

    enum KeyType {
        @EnumValue("CMK")
        CMK,
        @EnumValue("DATA_KEY")
        DATA_KEY;
    }

    /**
     * Tries to set AWS credential system properties if any exist in authConfig
     * for the specified customer
     *
     * @param customerUUID the customer the authConfig should be retrieved for
     */
    private void setUserCredentials(UUID customerUUID) {
        Properties p = new Properties(System.getProperties());
        ObjectNode authConfig = getAuthConfig(customerUUID);
        if (authConfig != null) {
            LOG.info("Setting credentials from AWS KMS configuration for AWS client");
            // Try using a kms_config if one exists
            JsonNode accessKeyId = authConfig.get("AWS_ACCESS_KEY_ID");
            JsonNode secretAccessKey = authConfig.get("AWS_SECRET_ACCESS_KEY");
            JsonNode region = authConfig.get("AWS_REGION");
            if (accessKeyId != null && secretAccessKey != null && region != null) {
                p.setProperty("aws.accessKeyId", accessKeyId.asText());
                p.setProperty("aws.secretKey", secretAccessKey.asText());
                p.setProperty("aws.region", region.asText());
            } else {
                LOG.info(
                        "Defaulting to attempt to use instance profile credentials for AWS client"
                );
            }
        } else {
            LOG.info("Setting credentials from AWS cloud provider for AWS client");
            // Otherwise, try using cloud provider credentials
            Provider provider = Provider.get(customerUUID, CloudType.aws);
            if (provider != null) {
                Map<String, String> config = provider.getConfig();
                String accessKeyId = config.get("AWS_ACCESS_KEY_ID");
                String secretAccessKey = config.get("AWS_SECRET_ACCESS_KEY");
                String region = ((Region) provider.regions.toArray()[0]).code;
                if (accessKeyId != null && secretAccessKey != null && region != null) {
                    p.setProperty("aws.accessKeyId", accessKeyId);
                    p.setProperty("aws.secretKey", secretAccessKey);
                    p.setProperty("aws.region", region);
                }
            }
        }
        System.setProperties(p);
    }

    /**
     * Instantiates a AWSKMS client to send requests to
     *
     * @param creds are the required credentials to get a client instance
     * @return a AWSKMS client
     */
    protected AWSKMS getClient(UUID customerUUID) {
        // Rely on the AWS credential provider chain
        // We set system properties as the first-choice option
        // To debug, a user can set AWS_ACCESS_KEY_ID, AWS_SECRET_KEY, AWS_REGION env vars
        // which will override any existing KMS configuration credentials
        // https://docs.aws.amazon.com/sdk-for-java/v1/developer-guide/credentials.html
        setUserCredentials(customerUUID);
        return AWSKMSClientBuilder.defaultClient();
    }

    /**
     * A method to prefix an alias base name with the proper format for AWS
     *
     * @param aliasName the name of the alias
     * @return the basename of the alias prefixed with 'alias/'
     */
    private String generateAliasName(String aliasName) {
        final String aliasNameBase = "alias/%s";
        return String.format(aliasNameBase, aliasName);
    }

    /**
     * Creates a new AWS KMS alias to be able to search for the CMK by
     *
     * @param universeUUID will be the name of the alias
     * @param customerUUID is the customer the alias is being created for
     * @param kId is the CMK that the alias should target
     */
    private void createAlias(UUID customerUUID, String kId, String aliasName) {
        final String aliasNameBase = "alias/%s";
        final CreateAliasRequest req = new CreateAliasRequest()
                .withAliasName(aliasName)
                .withTargetKeyId(kId);
        getClient(customerUUID).createAlias(req);
    }

    /**
     * Try to retreive an AWS KMS alias by its name
     *
     * @param customerUUID is the customer that the alias was created for
     * @param aliasName is the name of the alias in AWS
     * @return the alias if found, or null otherwise
     */
    private AliasListEntry getAlias(UUID customerUUID, String aliasName) {
        ListAliasesRequest req = new ListAliasesRequest().withLimit(100);
        AliasListEntry uniAlias = null;
        boolean done = false;
        AWSKMS client = getClient(customerUUID);
        while (!done) {
            ListAliasesResult result = client.listAliases(req);
            for (AliasListEntry alias : result.getAliases()) {
                if (alias.getAliasName().equals(aliasName)) {
                    uniAlias = alias;
                    break;
                }
            }
            req.setMarker(result.getNextMarker());
            if (!result.getTruncated()) done = true;
        }
        return uniAlias;
    }

    private String getCMKArn(UUID customerUUID, String cmkId) {
        String cmkArn = null;
        ListKeysRequest req = new ListKeysRequest().withLimit(1000);
        boolean done = false;
        AWSKMS client = getClient(customerUUID);
        while (!done) {
            ListKeysResult result = client.listKeys(req);
            for (KeyListEntry key : result.getKeys()) {
                if (key.getKeyId().equals(cmkId)) {
                    cmkArn = key.getKeyArn();
                    break;
                }
            }
            req.setMarker(result.getNextMarker());
            if (!result.getTruncated()) done = true;
        }
        return cmkArn;
    }

    /**
     * Decrypts the universe master key ciphertext blob into plaintext using the universe CMK
     *
     * @param customerUUID is the customer the universe belongs to
     * @param encryptedUniverseKey is the ciphertext blob of the universe master key
     * @return a plaintext byte array of the universe master key
     */
    private byte[] decryptUniverseKey(UUID customerUUID, byte[] encryptedUniverseKey) {
        if (encryptedUniverseKey == null) return null;
        ByteBuffer encryptedKeyBuffer = ByteBuffer.wrap(encryptedUniverseKey);
        encryptedKeyBuffer.rewind();
        final DecryptRequest req = new DecryptRequest().withCiphertextBlob(encryptedKeyBuffer);
        ByteBuffer decryptedKeyBuffer = getClient(customerUUID).decrypt(req).getPlaintext();
        decryptedKeyBuffer.rewind();
        byte[] decryptedUniverseKey = new byte[decryptedKeyBuffer.remaining()];
        decryptedKeyBuffer.get(decryptedUniverseKey);
        return decryptedUniverseKey;
    }

    /**
     * Generates a universe master key using the universe alias targetted CMK
     *
     * @param customerUUID the customer this is for
     * @param cmkId the Id of the CMK that the alias for this universe is pointing to
     * @param algorithm is the desired universe master key algorithm
     * @param keySize is the desired universe master key size
     * @return a ciphertext blob of the universe master key
     */
    private byte[] generateDataKey(
            UUID customerUUID,
            String cmkId,
            String algorithm,
            int keySize
    ) {
        final String keySpecBase = "%s_%s";
        final GenerateDataKeyWithoutPlaintextRequest dataKeyRequest =
                new GenerateDataKeyWithoutPlaintextRequest()
                        .withKeyId(cmkId)
                        .withKeySpec(
                                String.format(keySpecBase, algorithm, Integer.toString(keySize))
                        );
        ByteBuffer encryptedKeyBuffer =
                getClient(customerUUID)
                        .generateDataKeyWithoutPlaintext(dataKeyRequest)
                        .getCiphertextBlob();
        encryptedKeyBuffer.rewind();
        byte[] encryptedKeyBytes = new byte[encryptedKeyBuffer.remaining()];
        encryptedKeyBuffer.get(encryptedKeyBytes);
        return encryptedKeyBytes;
    }

    @Override
    protected AwsAlgorithm[] getSupportedAlgorithms() { return AwsAlgorithm.values(); }

    private String createOrRetrieveCMKWithAlias(
            UUID customerUUID,
            String policy,
            String description,
            String aliasBaseName
    ) {
        String result = null;
        final String aliasName = generateAliasName(aliasBaseName);
        AliasListEntry existingAlias = getAlias(customerUUID, aliasName);
        if (existingAlias == null) {
            // Create the CMK
            CreateKeyRequest req = new CreateKeyRequest();
            if (description != null) req = req.withDescription(description);
            if (policy != null && policy.length() > 0) req = req.withPolicy(policy);
            CreateKeyResult createResult = getClient(customerUUID).createKey(req);
            final String kId = createResult.getKeyMetadata().getKeyId();
            // Point it to an alias
            createAlias(customerUUID, kId, aliasName);
            result = kId;
        } else {
            result = existingAlias.getTargetKeyId();
        }
        return result;
    }

    private String createOrRetrieveCMK(
            UUID customerUUID,
            UUID universeUUID,
            String policy
    ) {
        return createOrRetrieveCMKWithAlias(
                customerUUID,
                policy,
                "Yugabyte Universe Key",
                universeUUID.toString()
        );
    }

    @Override
    protected byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        byte[] result = null;
        String typeString = config.get("key_type");
        KeyType type = typeString == null || typeString.length() == 0 ?
                KeyType.DATA_KEY : KeyType.valueOf(typeString);
        String cmkId = createOrRetrieveCMK(
                customerUUID,
                universeUUID,
                config.get("cmk_policy")
        );
        switch (type) {
            case CMK:
                result = getCMKArn(customerUUID, cmkId).getBytes();
                break;
            default:
            case DATA_KEY:
                final String algorithm = config.get("algorithm");
                final int keySize = Integer.parseInt(config.get("key_size"));
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
                result = generateDataKey(customerUUID, cmkId, algorithm, keySize);
                if (result != null && result.length > 0) {
                    addKeyRef(customerUUID, universeUUID, result);
                }
                break;
        }
        return result;
    }

    @Override
    protected byte[] rotateKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        final String aliasName = generateAliasName(universeUUID.toString());
        final AliasListEntry alias = getAlias(customerUUID, aliasName);
        final String cmkId = alias.getTargetKeyId();
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        return generateDataKey(customerUUID, cmkId, algorithm, keySize);
    }

    public byte[] retrieveKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    ) {
        byte[] keyVal = null;
        try {
            String typeString = config.get("key_type");
            KeyType type = typeString == null || typeString.length() == 0 ?
                    KeyType.DATA_KEY : KeyType.valueOf(typeString);
            switch (type) {
                case CMK:
                    keyVal = keyRef;
                    break;
                default:
                case DATA_KEY:
                    keyVal = decryptUniverseKey(customerUUID, keyRef);
                    if (keyVal == null) LOG.warn("Could not retrieve key from key ref");
                    else this.util.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
                    break;
            }
        } catch (Exception e) {
            final String errMsg = "Error occurred retrieving encryption key";
            LOG.error(errMsg, e);
            throw new RuntimeException(errMsg, e);
        }
        return keyVal;
    }
}

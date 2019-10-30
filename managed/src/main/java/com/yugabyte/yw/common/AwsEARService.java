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
import com.amazonaws.services.kms.model.ListAliasesRequest;
import com.amazonaws.services.kms.model.ListAliasesResult;
import com.amazonaws.services.kms.model.UpdateAliasRequest;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.EncryptionAtRestManager.KeyProvider;
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

    /**
     * Tries to set AWS credential system properties if any exist in authConfig
     * for the specified customer
     *
     * @param customerUUID the customer the authConfig should be retrieved for
     */
    private void setUserCredentials(UUID customerUUID) {
        ObjectNode authConfig = getAuthConfig(customerUUID);
        if (authConfig == null) return;
        JsonNode accessKeyId = authConfig.get("AWS_ACCESS_KEY_ID");
        JsonNode secretAccessKey = authConfig.get("AWS_SECRET_ACCESS_KEY");
        JsonNode region = authConfig.get("AWS_REGION");
        Properties p = new Properties(System.getProperties());
        if (accessKeyId != null && secretAccessKey != null && region != null) {
            p.setProperty("aws.accessKeyId", accessKeyId.asText());
            p.setProperty("aws.secretKey", secretAccessKey.asText());
            p.setProperty("aws.region", region.asText());
            System.setProperties(p);
        } else {
            LOG.info("Defaulting to attempt to use instance profile credentials for AWS client");
        }
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
    private void createAlias(UUID universeUUID, UUID customerUUID, String kId) {
        final String aliasNameBase = "alias/%s";
        final CreateAliasRequest req = new CreateAliasRequest()
                .withAliasName(generateAliasName(universeUUID.toString()))
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
        while (!done) {
            ListAliasesResult result = getClient(customerUUID).listAliases(req);
            for (AliasListEntry alias : result.getAliases()) {
                if (alias.getAliasName().equals(generateAliasName(aliasName))) {
                    uniAlias = alias;
                    break;
                }
            }
            req.setMarker(result.getNextMarker());
            if (!result.getTruncated()) {
                done = true;
            }
        }

        return uniAlias;
    }

    /**
     * Updates the CMK that an alias targets
     *
     * @param customerUUID the customer the CMK and alias belong to
     * @param universeUUID the name of the alias
     * @param newTargetKeyId the new CMK that the alias should target
     */
    private void updateAliasTarget(UUID customerUUID, UUID universeUUID, String newTargetKeyId) {
        UpdateAliasRequest req = new UpdateAliasRequest()
                .withAliasName(generateAliasName(universeUUID.toString()))
                .withTargetKeyId(newTargetKeyId);
        getClient(customerUUID).updateAlias(req);
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
    private byte[] generateDataKey(UUID customerUUID, String cmkId, String algorithm, int keySize) {
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

    @Override
    protected byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        final String customPolicy = config.get("cmk_policy");
        CreateKeyRequest req = new CreateKeyRequest()
                .withDescription("Yugabyte Universe Key");
        if (customPolicy != null && customPolicy.length() > 0) req = req.withPolicy(customPolicy);
        final CreateKeyResult result = getClient(customerUUID).createKey(req);
        final String kId = result.getKeyMetadata().getKeyId();
        if (getAlias(customerUUID, universeUUID.toString()) == null) {
            createAlias(universeUUID, customerUUID, kId);
        } else {
            updateAliasTarget(customerUUID, universeUUID, kId);
        }
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        return generateDataKey(customerUUID, kId, algorithm, keySize);
    }

    @Override
    protected byte[] rotateKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        final AliasListEntry alias = getAlias(customerUUID, universeUUID.toString());
        final String cmkId = alias.getTargetKeyId();
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        return generateDataKey(customerUUID, cmkId, algorithm, keySize);
    }

    @Override
    public byte[] retrieveKeyWithService(UUID customerUUID, byte[] keyRef) {
        byte[] result = null;
        try {
            result = decryptUniverseKey(customerUUID, keyRef);
            if (result == null) LOG.warn("Could not retrieve key from key ref");
        } catch (Exception e) {
            final String errMsg = "Error occurred retrieving encryption key";
            LOG.error(errMsg, e);
            throw new RuntimeException(errMsg, e);
        }
        return result;
    }
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Singleton;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import java.util.Map;

@Singleton
public class AccessManager {
    public static final Logger LOG = LoggerFactory.getLogger(AccessManager.class);

    @Inject
    ShellProcessHandler shellProcessHandler;

    @Inject
    play.Configuration appConfig;

    public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
    public static final String YB_CLOUD_ACCESS_COMMAND = "access";

    public enum KeyType {
        PUBLIC,
        PRIVATE;

        public String getExtension() {
            switch(this) {
                case PUBLIC:
                    return ".pub";
                case PRIVATE:
                    return ".pem";
                default:
                    return null;
            }
        }
    }

    private String getKeyFilePath(UUID providerUUID) {
        String keyBasePathName = appConfig.getString("yb.keys.basePath");

        if (keyBasePathName == null || ! new File(keyBasePathName).exists()) {
            throw new RuntimeException("yb.keys.basePath is null or doesn't exist.");
        }

        File keyFilePath = new File(keyBasePathName, providerUUID.toString());
        if (keyFilePath.isDirectory() || keyFilePath.mkdirs()) {
            return keyFilePath.getAbsolutePath();
        }

        throw new RuntimeException("Unable to create key file path " + keyFilePath.getAbsolutePath());
    }

    private List<String> getBaseCommand(UUID providerUUID) {
        Provider cloudProvider = Provider.find.byId(providerUUID);

        List<String> baseCommand = new ArrayList<>();
        baseCommand.add(YBCLOUD_SCRIPT);
        baseCommand.add(cloudProvider.code);

        baseCommand.add(YB_CLOUD_ACCESS_COMMAND);
        return baseCommand;
    }

    // This method would upload the provided key file to the provider key file path.
    public AccessKey.KeyInfo uploadKeyFile(UUID providerUUID, File uploadedFile,
                                           String keyCode, KeyType keyType) {
        String keyFilePath = getKeyFilePath(providerUUID);
        Path source = Paths.get(uploadedFile.getAbsolutePath());
        Path destination = Paths.get(keyFilePath, keyCode + keyType.getExtension());
        if (!Files.exists(source)) {
            throw new RuntimeException("Key file " + source.getFileName() + " not found.");
        }
        if (Files.exists(destination) ) {
            throw new RuntimeException("File " + destination.getFileName() + " already exists.");
        }

        try {
            Files.move(source, destination);
        } catch (IOException e) {
            LOG.error(e.getMessage(), e);
            throw new RuntimeException("Unable to upload key file " + source.getFileName());
        }

        String keyFileName = destination.getFileName().toString();
        AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
        if (keyType == AccessManager.KeyType.PUBLIC) {
            keyInfo.publicKey = keyFileName;
        } else {
            keyInfo.privateKey = keyFileName;
        }
        return keyInfo;
    }

    // This method would create a public/private key file and upload that to
    // the provider cloud account. And store the credentials file in the keyFilePath
    // and return the file names.
    public AccessKey.KeyInfo addKey(UUID providerUUID, String keyCode) {
        List<String> command = new ArrayList<String>();
        String keyFilePath = getKeyFilePath(providerUUID);
        command.add("add-key");
        command.add("--key_pair_name");
        command.add(keyCode);
        command.add("--key_file_path");
        command.add(keyFilePath);

        JsonNode response = executeCommand(providerUUID, command);
        if (response.has("error")) {
            throw new RuntimeException(response.get("error").asText());
        }

        AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
        keyInfo.publicKey = response.get("public_key").asText();
        keyInfo.privateKey = response.get("private_key").asText();
        return keyInfo;
    }

    public JsonNode listKeys(UUID providerUUID) {
        return executeCommand(providerUUID, ImmutableList.of("list-keys"));
    }

    public JsonNode listRegions(UUID providerUUID) {
        return executeCommand(providerUUID, ImmutableList.of("list-regions"));
    }

    private JsonNode executeCommand(UUID providerUUID, List<String> commandArgs) {
        List<String> command = getBaseCommand(providerUUID);
        command.addAll(commandArgs);

        Provider provider = Provider.get(providerUUID);
        ShellProcessHandler.ShellResponse response = shellProcessHandler.run(command, provider.getConfig());

        if (response.code == 0) {
            return Json.parse(response.message);
        } else {
            LOG.error(response.message);
            return ApiResponse.errorJSON("AccessManager failed to execute");
        }
    }
}


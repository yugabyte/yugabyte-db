// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Singleton;
import java.io.File;
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

    // TODO: handle case of providing key file to the addKey call.
    // This method would create a public/private key file and upload that to
    // the provider cloud account. And store the credentials file in the keyFilePath
    // and return the file names.
    public JsonNode addKey(UUID providerUUID, String keyName) {
        List<String> command = getBaseCommand(providerUUID);
        String keyFilePath;
        try {
            keyFilePath = getKeyFilePath(providerUUID);
        } catch (RuntimeException re) {
            return ApiResponse.errorJSON(re.getMessage());
        }

        command.add("add-key");
        command.add("--key_pair_name");
        command.add(keyName);
        command.add("--key_file_path");
        command.add(keyFilePath);
        return executeCommand(command);
    }

    public JsonNode listKeys(UUID providerUUID) {
        List<String> command = getBaseCommand(providerUUID);
        command.add("list-keys");

        return executeCommand(command);
    }

    public JsonNode listRegions(UUID providerUUID) {
        List<String> command = getBaseCommand(providerUUID);
        command.add("list-regions");
        return executeCommand(command);
    }

    private JsonNode executeCommand(List<String> command) {
        // TODO: We need to store access credentials at provider level and provide those
        // as environment variables.
        // we should have a method on the provider like getCredentials, which would
        // fetch the necessary crendential env variables.
        Map<String, String> cloudCredentials = new HashMap<>();
        ShellProcessHandler.ShellResponse response = shellProcessHandler.run(command, cloudCredentials);

        if (response.code == 0) {
            return Json.parse(response.message);
        } else {
            LOG.error(response.message);
            return ApiResponse.errorJSON("AccessManager failed to execute");
        }
    }
}


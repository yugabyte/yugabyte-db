// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import javax.persistence.PersistenceException;
import java.util.List;
import java.util.UUID;

public class AccessKeyController extends AuthenticatedController {
    @Inject
    FormFactory formFactory;

    @Inject
    AccessManager accessManager;

    public static final Logger LOG = LoggerFactory.getLogger(AccessKeyController.class);

    public Result index(UUID customerUUID, UUID providerUUID, String keyCode) {
        String validationError = validateUUIDs(customerUUID, providerUUID);
        if (validationError != null) {
            return ApiResponse.error(BAD_REQUEST, validationError);
        }

        AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
        if (accessKey == null) {
            return ApiResponse.error(BAD_REQUEST, "KeyCode not found: " + keyCode);
        }
        return ApiResponse.success(accessKey);
    }

    public Result list(UUID customerUUID, UUID providerUUID) {
        String validationError = validateUUIDs(customerUUID, providerUUID);
        if (validationError != null) {
            return ApiResponse.error(BAD_REQUEST, validationError);
        }

        List<AccessKey> accessKeys;
        try {
            accessKeys = AccessKey.getAll(providerUUID);
        } catch (Exception e) {
            return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
        }
        return ApiResponse.success(accessKeys);
    }


    public Result create(UUID customerUUID, UUID providerUUID) {
        Form<AccessKeyFormData> formData = formFactory.form(AccessKeyFormData.class).bindFromRequest();
        String validationError = validateUUIDs(customerUUID, providerUUID);
        if (validationError != null) {
            return ApiResponse.error(BAD_REQUEST, validationError);
        }

        if (formData.hasErrors()) {
            return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
        }

        AccessKey accessKey = AccessKey.get(providerUUID, formData.get().keyCode);
        if (accessKey != null) {
            return ApiResponse.error(BAD_REQUEST, "Duplicate keycode: " + formData.get().keyCode);
        }

        try {
            JsonNode response = accessManager.addKey(providerUUID, formData.get().keyCode);
            if (response.has("error")) {
                return ApiResponse.error(INTERNAL_SERVER_ERROR, response.get("error"));
            }

            AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
            keyInfo.publicKey = response.get("public_key").asText();
            keyInfo.privateKey = response.get("private_key").asText();
            accessKey = AccessKey.create(providerUUID, formData.get().keyCode, keyInfo);
            return ApiResponse.success(accessKey);
        } catch(Exception e) {
            return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
        }
    }

    public Result delete(UUID customerUUID, UUID providerUUID, String keyCode) {
        String validationError = validateUUIDs(customerUUID, providerUUID);
        if (validationError != null) {
            return ApiResponse.error(BAD_REQUEST, validationError);
        }

        AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
        if (accessKey == null) {
            return ApiResponse.error(BAD_REQUEST, "KeyCode not found: " + keyCode);
        }

        if (accessKey.delete()) {
            return ApiResponse.success("Deleted KeyCode: " + keyCode);
        } else {
            return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete KeyCode: " + keyCode);
        }
    }

    public Result update(UUID customerUUID, UUID providerUUID, String keyCode) {
        String validationError = validateUUIDs(customerUUID, providerUUID);
        if (validationError != null) {
            return ApiResponse.error(BAD_REQUEST, validationError);
        }

        Form<AccessKeyFormData> formData = formFactory.form(AccessKeyFormData.class).bindFromRequest();
        if (formData.hasErrors()) {
            return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
        }

        AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
        if (accessKey == null) {
            return ApiResponse.error(BAD_REQUEST, "KeyCode not found: " + keyCode);
        }

        try {
            accessKey.setKeyInfo(formData.get().keyInfo);
            accessKey.update();
            return ApiResponse.success("Updated KeyCode: " + keyCode);
        } catch (Exception e) {
            return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
        }
    }

    private String validateUUIDs(UUID customerUUID, UUID providerUUID) {
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            return "Invalid Customer UUID: " + customerUUID;
        }
        Provider provider = Provider.find.byId(providerUUID);
        if (provider == null) {
            return "Invalid Provider UUID: " + providerUUID;
        }
        return null;
    }
}

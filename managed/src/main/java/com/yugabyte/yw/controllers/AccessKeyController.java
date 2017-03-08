// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Http;
import play.mvc.Result;

import java.io.File;
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
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      UUID regionUUID = formData.get().regionUUID;
      Region region = Region.get(customerUUID, providerUUID, regionUUID);
      if (region == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Provider/Region UUID");
      }

      String keyCode = formData.get().keyCode;
      AccessManager.KeyType keyType = formData.get().keyType;

      AccessKey accessKey;
      // Check if a public/private key was uploaded as part of the request
      Http.MultipartFormData multiPartBody = request().body().asMultipartFormData();
      try {
        if (multiPartBody != null) {
          Http.MultipartFormData.FilePart filePart = multiPartBody.getFile("keyFile");
          File uploadedFile = (File) filePart.getFile();
          if (keyType == null || uploadedFile == null) {
            return ApiResponse.error(BAD_REQUEST, "keyType and keyFile params required.");
          }
          accessKey = accessManager.uploadKeyFile(region.uuid, uploadedFile, keyCode, keyType);
        } else {
          accessKey = accessManager.addKey(region.uuid, keyCode);
        }
      } catch(RuntimeException re) {
        return ApiResponse.error(INTERNAL_SERVER_ERROR, re.getMessage());
      }
      return ApiResponse.success(accessKey);
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

    private String validateUUIDs(UUID customerUUID, UUID providerUUID) {
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            return "Invalid Customer UUID: " + customerUUID;
        }
        Provider provider = Provider.find.where()
            .eq("customer_uuid", customerUUID)
            .idEq(providerUUID).findUnique();
        if (provider == null) {
            return "Invalid Provider UUID: " + providerUUID;
        }
        return null;
    }
}

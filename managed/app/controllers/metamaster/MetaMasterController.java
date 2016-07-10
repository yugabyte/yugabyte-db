// Copyright (c) YugaByte, Inc.

package controllers.metamaster;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;

import forms.metamaster.MetaMasterFormData;
import helpers.ApiResponse;
import models.metamaster.MetaMasterEntry;
import models.metamaster.MetaMasterEntry.MasterInfo;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Controller;
import play.mvc.Result;

public class MetaMasterController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterController.class);

  @Inject
  FormFactory formFactory;

  public Result get(UUID instanceUUID) {
    // Lookup the entry for the instanceUUID.
    List<MasterInfo> masters = MetaMasterEntry.get(instanceUUID);
    // If the entry was not found, return an error.
    if (masters == null) {
      return ApiResponse.error(BAD_REQUEST, "Could not find instanceUUID " + instanceUUID);
    }
    // Return the result.
    return ok(getMasterAddresses(masters));
  }

  public Result upsert(UUID instanceUUID) {
    // Get the params for the request.
    Form<MetaMasterFormData> formData =
        formFactory.form(MetaMasterFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    MetaMasterFormData params = formData.get();

    // Insert/update the entry.
    MetaMasterEntry.upsert(instanceUUID, params.masters);
    return ok(getMasterAddresses(params.masters));
  }

  public Result delete(UUID instanceUUID) {
    // Delete the entry for the instanceUUID.
    boolean result = MetaMasterEntry.delete(instanceUUID);
    if (!result) {
      return ApiResponse.error(BAD_REQUEST, "Could not find instanceUUID " + instanceUUID);
    }
    ObjectNode responseJson = Json.newObject();
    responseJson.put("instance", instanceUUID.toString());
    return ok(responseJson);
  }

  private ObjectNode getMasterAddresses(List<MetaMasterEntry.MasterInfo> masters) {
    // Compose a comma separated list of host:port's.
    StringBuilder sb = new StringBuilder();
    for (MasterInfo master : masters) {
      if (sb.length() != 0) {
        sb.append(",");
      }
      sb.append(master.host);
      sb.append(":");
      sb.append(master.port);
    }
    ObjectNode responseJson = Json.newObject();
    responseJson.put("masters", sb.toString());
    return responseJson;
  }
}

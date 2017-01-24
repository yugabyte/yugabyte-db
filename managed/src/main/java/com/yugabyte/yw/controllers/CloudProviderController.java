// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.forms.OnPremFormData;

import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import com.google.inject.Inject;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;

public class CloudProviderController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderController.class);

  @Inject
  FormFactory formFactory;

  /**
   * GET endpoint for listing providers
   * @return JSON response with provider's
   */
  public Result list() {
    List<Provider> providerList = Provider.find.all();
    return ok(Json.toJson(providerList));
  }

  /**
   * POST endpoint for creating new providers
   * @return JSON response of newly created provider
   */
  public Result create() {
    Form<Provider> formData = formFactory.form(Provider.class).bindFromRequest();
    ObjectNode responseJson = Json.newObject();

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    try {
      Provider p = Provider.create(formData.get().code, formData.get().name);
      return ok(Json.toJson(p));
    } catch (Exception e) {
      // TODO: Handle exception and print user friendly message
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
  }

  /**
   * POST endpoint for setting up an on-prem deployment
   * @return JSON response of newly created provider
   */
  public Result setupOnPrem() {
    Form<OnPremFormData> formData = formFactory.form(OnPremFormData.class).bindFromRequest();
    ObjectNode responseJson = Json.newObject();

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    // Get provider data or create a new provider.
    OnPremFormData.IdData identification = formData.get().identification;
    if (identification == null) {
      responseJson.put("error", "Field <identification> cannot be empty!");
      return badRequest(responseJson);
    }

    Provider p = null;
    if (identification.uuid == null) {
      if (identification.type == null || identification.name == null) {
        responseJson.put(
            "error", "Field <identification> must have either <uuid> or <name> and <type>");
        return badRequest(responseJson);
      }

      p = Provider.get(identification.name);
      if (p == null) {
        p = Provider.create(identification.type, identification.name);
        LOG.info("Created provider " + p);
      }
    } else {
      p = Provider.find.byId(identification.uuid);
    }

    OnPremFormData.CloudData description = formData.get().description;
    if (description != null) {
      if (description.instance_types != null) {
        for (OnPremFormData.InstanceTypeData i : description.instance_types) {
          InstanceType it = InstanceType.get(p.code, i.code);
          if (it == null) {
            // TODO: instance type metadata?
            it = InstanceType.upsert(
                p.code,
                i.code,
                0, // numCores
                0.0, // memSizeGB
                0, // volumeCount
                0, // volumeSizeGB
                InstanceType.VolumeType.SSD,
                new InstanceTypeDetails()
                );
          }
        }
      }
      if (description.regions != null) {
        for (OnPremFormData.RegionData r : description.regions) {
          Region region = Region.getByCode(r.code);
          if (region == null) {
            // TODO: region name vs code?
            region = Region.create(
                p,
                r.code, // code
                r.code, // name
                "" // ybImage
                );
          }
          if (r.zones != null) {
            for (OnPremFormData.ZoneData z : r.zones) {
              AvailabilityZone zone = AvailabilityZone.getByCode(z.code);
              if (zone == null) {
                // TODO: zone name vs node?
                zone = AvailabilityZone.create(
                    region,
                    z.code, // code
                    z.code, // name
                    "" // subnet
                    );
              }
              if (z.nodes != null) {
                for (OnPremFormData.NodeData n : z.nodes) {
                  // TODO: placeholder for node processing
                }
              }
            }
          }
        }
      }
    }

    // TODO: placeholder
    OnPremFormData.AccessData access = formData.get().access;
    if (access != null) {
      responseJson.set("access", Json.toJson(access));
    }

    return ok(responseJson);
  }
}

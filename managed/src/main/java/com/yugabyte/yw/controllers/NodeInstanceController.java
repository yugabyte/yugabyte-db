// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Customer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Inject;
import com.yugabyte.yw.models.AvailabilityZone;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData;

import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

public class NodeInstanceController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;

  public static final Logger LOG = LoggerFactory.getLogger(NodeInstanceController.class);

  /**
   * GET endpoint for Node data
   * @param customerUuid the customer UUID
   * @param nodeUuid the node UUID
   * @return JSON response with Node data
   */
  public Result get(UUID customerUuid, UUID nodeUuid) {
    String error = validateParams(customerUuid, null /* zoneUuid */);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    try {
      NodeInstance node = NodeInstance.get(nodeUuid);
      return ApiResponse.success(node);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * GET endpoint for getting all unused nodes under a zone
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response with list of nodes
   */
  public Result listByZone(UUID customerUuid, UUID zoneUuid) {
    String error = validateParams(customerUuid, zoneUuid);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    try {
      List<NodeInstance> nodes = NodeInstance.listByZone(zoneUuid, null /* instanceTypeCode */);
      return ApiResponse.success(nodes);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Result listByProvider(UUID customerUUID, UUID providerUUID) {
    List<NodeInstance> regionList;
    try {
      regionList = NodeInstance.listByProvider(providerUUID);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return ApiResponse.success(regionList);
  }
  /**
   * POST endpoint for creating a new Node
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response of newly created Node
   */
  public Result create(UUID customerUuid, UUID zoneUuid) {
    String error = validateParams(customerUuid, zoneUuid);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    Form<NodeInstanceFormData> formData = formFactory.form(NodeInstanceFormData.class).bindFromRequest();
    try {
      NodeInstance node = NodeInstance.create(
        zoneUuid,
        formData.get());
      return ApiResponse.success(node);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private String validateParams(UUID customerUuid, UUID zoneUuid) {
    String e = null;
    e = validateClassByUuid(Customer.class, customerUuid);
    if (e != null) return e;
    e = validateClassByUuid(AvailabilityZone.class, zoneUuid);
    if (e != null) return e;
    return e;
  }

  private String validateClassByUuid(Class c, UUID uuid) {
    try {
      if (uuid != null && c.getMethod("get", UUID.class).invoke(null, uuid) == null) {
        return "Invalid " + c.getName() + "UUID: " + uuid;
      }
    } catch (Throwable t) {
      return t.getMessage();
    }
    return null;
  }
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.CertificateInfo;
import play.data.validation.Constraints;
import play.mvc.Http;

import java.util.UUID;

/** Class to capture request params for toggle_tls API */
public class ToggleTlsParams {
  public UpgradeParams.UpgradeOption upgradeOption = UpgradeParams.UpgradeOption.ROLLING_UPGRADE;

  @Constraints.Required() public boolean enableNodeToNodeEncrypt;

  @Constraints.Required() public boolean enableClientToNodeEncrypt;

  public UUID rootCA = null;

  // Verifies the ToggleTlsParams by comparing with the existing
  // UniverseDefinitionTaskParams, returns YWError object if invalid else null
  public YWResults.YWError verifyParams(UniverseDefinitionTaskParams universeParams) {
    boolean existingEnableClientToNodeEncrypt =
        universeParams.getPrimaryCluster().userIntent.enableClientToNodeEncrypt;
    boolean existingEnableNodeToNodeEncrypt =
        universeParams.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt;
    UUID existingRootCA = universeParams.rootCA;

    if (upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE) {
      return new YWResults.YWError(
          "TLS upgrade can be performed either rolling or non-rolling way.");
    }

    if (this.enableClientToNodeEncrypt == existingEnableClientToNodeEncrypt
        && this.enableNodeToNodeEncrypt == existingEnableNodeToNodeEncrypt) {
      return new YWResults.YWError(
          "No changes in Tls parameters, cannot perform update operation.");
    }

    if (rootCA != null && CertificateInfo.get(rootCA) == null) {
      return new YWResults.YWError("No valid rootCA found for UUID: " + rootCA);
    }

    if (existingRootCA != null && rootCA != null && !existingRootCA.equals(rootCA)) {
      return new YWResults.YWError("Cannot update root certificate, if already created.");
    }

    return null;
  }

  public static ToggleTlsParams bindFromFormData(ObjectNode formData) {
    ToggleTlsParams params = new ToggleTlsParams();
    JsonNode upgradeOption = formData.get("upgradeOption");
    JsonNode nodeToNode = formData.get("enableNodeToNodeEncrypt");
    JsonNode clientToNode = formData.get("enableClientToNodeEncrypt");
    JsonNode rootCA = formData.get("rootCA");

    if (upgradeOption != null && upgradeOption.isTextual() && !upgradeOption.asText().isEmpty()) {
      try {
        params.upgradeOption = UpgradeParams.UpgradeOption.valueOf(upgradeOption.asText());
      } catch (IllegalArgumentException e) {
        throw new YWServiceException(
            Http.Status.BAD_REQUEST, "upgradeOption: Invalid upgrade option.");
      }
    }

    if (nodeToNode != null && nodeToNode.isBoolean()) {
      params.enableNodeToNodeEncrypt = nodeToNode.asBoolean();
    } else {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "enableNodeToNodeEncrypt: This field is required.");
    }

    if (clientToNode != null && clientToNode.isBoolean()) {
      params.enableClientToNodeEncrypt = clientToNode.asBoolean();
    } else {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "enableClientToNodeEncrypt: This field is required.");
    }

    if (rootCA != null && rootCA.isTextual() && !rootCA.asText().isEmpty()) {
      try {
        params.rootCA = UUID.fromString(rootCA.asText());
      } catch (IllegalArgumentException e) {
        throw new YWServiceException(Http.Status.BAD_REQUEST, "rootCA: Invalid Uuid String.");
      }
    }

    return params;
  }
}

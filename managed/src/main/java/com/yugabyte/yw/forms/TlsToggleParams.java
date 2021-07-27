// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = TlsToggleParams.Converter.class)
public class TlsToggleParams extends UpgradeTaskParams {

  public boolean enableNodeToNodeEncrypt = false;
  public boolean enableClientToNodeEncrypt = false;
  public boolean allowInsecure = true;
  public UUID rootCA = null;

  public TlsToggleParams() {}

  @JsonCreator
  public TlsToggleParams(
      @JsonProperty(value = "enableNodeToNodeEncrypt", required = true)
          boolean enableNodeToNodeEncrypt,
      @JsonProperty(value = "enableClientToNodeEncrypt", required = true)
          boolean enableClientToNodeEncrypt) {
    this.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
    this.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    boolean existingEnableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    boolean existingEnableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    UUID existingRootCA = universeDetails.rootCA;

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new YWServiceException(
          Status.BAD_REQUEST, "TLS toggle can be performed either rolling or non-rolling way.");
    }

    if (this.enableClientToNodeEncrypt == existingEnableClientToNodeEncrypt
        && this.enableNodeToNodeEncrypt == existingEnableNodeToNodeEncrypt) {
      throw new YWServiceException(
          Status.BAD_REQUEST, "No changes in Tls parameters, cannot perform update operation.");
    }

    if (existingRootCA != null && rootCA != null && !existingRootCA.equals(rootCA)) {
      throw new YWServiceException(
          Status.BAD_REQUEST, "Cannot update root certificate, if already created.");
    }

    if (rootCA != null && CertificateInfo.get(rootCA) == null) {
      throw new YWServiceException(Status.BAD_REQUEST, "No valid rootCA found for UUID: " + rootCA);
    }

    if (!CertificateInfo.isCertificateValid(rootCA)) {
      throw new YWServiceException(
          Status.BAD_REQUEST,
          "The certificate "
              + CertificateInfo.get(rootCA).label
              + " needs info. Update the cert and retry.");
    }

    if (rootCA != null
        && CertificateInfo.get(rootCA).certType == CertificateInfo.Type.CustomCertHostPath
        && !userIntent.providerType.equals(CloudType.onprem)) {
      throw new YWServiceException(
          Status.BAD_REQUEST, "Custom certificates are only supported for on-prem providers.");
    }

    if (!universeDetails.rootAndClientRootCASame
        || (universeDetails.rootCA != null
            && !universeDetails.rootCA.equals(universeDetails.clientRootCA))) {
      throw new YWServiceException(
          Status.BAD_REQUEST, "RootCA and ClientRootCA cannot be different for Upgrade.");
    }
  }

  public static class Converter extends BaseConverter<TlsToggleParams> {}
}

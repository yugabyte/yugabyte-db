// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import play.mvc.Http;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = TlsToggleParams.Converter.class)
public class TlsToggleParams extends UpgradeTaskParams {

  public boolean enableNodeToNodeEncrypt = false;
  public boolean enableClientToNodeEncrypt = false;
  public boolean allowInsecure = true;

  // below fields are already inherited from UniverseDefinitionTaskParams
  //  public UUID rootCA = null;
  //  public UUID clientRootCA = null;
  //  public Boolean rootAndClientRootCASame = null;

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
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    boolean existingEnableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    boolean existingEnableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    UUID existingRootCA = universeDetails.rootCA;
    UUID existingClientRootCA = universeDetails.getClientRootCA();

    // Due to a bug, temporarily disable rolling upgrade for TLS toggle.
    if (upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "TLS toggle can only be performed in a non-rolling manner.");
    }

    if (this.enableClientToNodeEncrypt == existingEnableClientToNodeEncrypt
        && this.enableNodeToNodeEncrypt == existingEnableNodeToNodeEncrypt) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "No changes in Tls parameters, cannot perform update operation.");
    }

    if (existingRootCA != null && rootCA != null && !existingRootCA.equals(rootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot update root certificate, if already created.");
    }

    if (existingClientRootCA != null
        && clientRootCA != null
        && !existingClientRootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot update client root certificate, if already created.");
    }

    if (!CertificateInfo.isCertificateValid(rootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "No valid root certificate found for UUID: " + rootCA);
    }

    if (!CertificateInfo.isCertificateValid(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "No valid client root certificate found for UUID: " + clientRootCA);
    }

    if (rootCA != null
        && CertificateInfo.get(rootCA).getCertType() == CertConfigType.CustomServerCert) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "CustomServerCert are only supported for Client to Server Communication.");
    }

    if (rootCA != null
        && CertificateInfo.get(rootCA).getCertType() == CertConfigType.CustomCertHostPath
        && !userIntent.providerType.equals(CloudType.onprem)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "CustomCertHostPath certificates are only supported for on-prem providers.");
    }

    if (clientRootCA != null
        && CertificateInfo.get(clientRootCA).getCertType() == CertConfigType.CustomCertHostPath
        && !userIntent.providerType.equals(Common.CloudType.onprem)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "CustomCertHostPath certificates are only supported for on-prem providers.");
    }

    // TODO: Add check that the userIntent is to use cert-manager
    if (rootCA != null
        && CertificateInfo.get(rootCA).getCertType() == CertConfigType.K8SCertManager
        && !userIntent.providerType.equals(CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "K8SCertManager certificates are only supported for k8s providers with cert-manager"
              + " configured.");
    }

    // TODO: Add check that the userIntent is to use cert-manager
    if (clientRootCA != null
        && CertificateInfo.get(clientRootCA).getCertType() == CertConfigType.K8SCertManager
        && !userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "K8SCertManager certificates are only supported for k8s providers with cert-manager"
              + " configured.");
    }

    if (rootAndClientRootCASame
        && enableNodeToNodeEncrypt
        && enableClientToNodeEncrypt
        && rootCA != null
        && clientRootCA != null
        && !rootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "RootCA and ClientRootCA cannot be different when rootAndClientRootCASame is true.");
    }
  }

  public static class Converter extends BaseConverter<TlsToggleParams> {}
}

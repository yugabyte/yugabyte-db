// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.CertificateInfo.Type;

import java.util.UUID;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = CertsRotateParams.Converter.class)
public class CertsRotateParams extends UpgradeTaskParams {

  public enum CertRotationType {
    None,
    ServerCert,
    RootCert
  }

  // If null, no upgrade will be performed on rootCA
  public UUID rootCA = null;
  // If null, no upgrade will be performed on clientRootCA
  public UUID clientRootCA = null;
  // if null, existing value will be used
  public Boolean rootAndClientRootCASame = null;

  @JsonIgnore public CertRotationType rootCARotationType = CertRotationType.None;
  @JsonIgnore public CertRotationType clientRootCARotationType = CertRotationType.None;

  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (!userIntent.providerType.equals(CloudType.kubernetes)) {
      verifyParamsForNormalUpgrade(universe);
    } else {
      verifyParamsForKubernetesUpgrade(universe);
    }
  }

  private void verifyParamsForNormalUpgrade(Universe universe) {
    // Validate request params on different constraints based on current universe state.
    // Update rootCA, clientRootCA and rootAndClientRootCASame to their desired final state.
    // Decide what kind of upgrade needs to be done on rootCA and clientRootCA.

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID currentRootCA = universe.getUniverseDetails().rootCA;
    UUID currentClientRootCA = universe.getUniverseDetails().clientRootCA;
    boolean currentRootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Cert upgrade cannot be non restart.");
    }

    // Make sure rootCA and clientRootCA respects the rootAndClientRootCASame property
    if (rootAndClientRootCASame != null
        && rootAndClientRootCASame
        && rootCA != null
        && clientRootCA != null
        && !rootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "RootCA and ClientRootCA cannot be different when rootAndClientRootCASame is true.");
    }

    // rootAndClientRootCASame is optional in request, if not present follow the existing flag
    if (rootAndClientRootCASame == null) {
      rootAndClientRootCASame = currentRootAndClientRootCASame;
    }

    boolean isRootCARequired =
        CertificateHelper.isRootCARequired(
            userIntent.enableNodeToNodeEncrypt,
            userIntent.enableClientToNodeEncrypt,
            rootAndClientRootCASame);
    boolean isClientRootCARequired =
        CertificateHelper.isClientRootCARequired(
            userIntent.enableNodeToNodeEncrypt,
            userIntent.enableClientToNodeEncrypt,
            rootAndClientRootCASame);

    // User cannot upgrade rootCA when there is no need for rootCA in the universe
    if (!isRootCARequired && rootCA != null && !rootCA.equals(currentRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "rootCA is not required with the current TLS parameters and cannot upgrade.");
    }

    // User cannot upgrade clientRootCA when there is no need for clientRootCA in the universe
    if (!isClientRootCARequired
        && clientRootCA != null
        && !clientRootCA.equals(currentClientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "clientRootCA is not required with the current TLS parameters and cannot upgrade.");
    }

    // Consider this case:
    // node-to-node: true, client-to-node: true, rootAndClientRootCASame: true
    // Initial state: rootCA: UUID, clientRootCA: null
    // Request: { rootCA: UUID, clientRootCA: null, rootAndClientRootCASame: false }
    // This is invalid request because clientRootCA is null currently and
    // user is trying to update rootAndClientRootCASame without setting clientRootCA
    if (isClientRootCARequired && currentClientRootCA == null && clientRootCA == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "clientRootCA is required with the current TLS parameters and cannot upgrade.");
    }

    if (rootCA != null && !rootCA.equals(currentRootCA)) {
      // When the request comes to this block, this is when actual upgrade on rootCA
      // needs to be done. Now check on what kind of upgrade it is, RootCert or ServerCert
      CertificateInfo rootCert = CertificateInfo.get(rootCA);
      if (rootCert == null) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Certificate not present: " + rootCA);
      }

      switch (rootCert.certType) {
        case SelfSigned:
          rootCARotationType = CertRotationType.RootCert;
          break;
        case CustomCertHostPath:
          if (!userIntent.providerType.equals(CloudType.onprem)) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                "Certs of type CustomCertHostPath can only be used for on-prem universes.");
          }
          if (rootCert.getCustomCertInfo() == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "The certificate %s needs info. Update the cert and retry.", rootCert.label));
          }
          if (currentRootCA != null && !CertificateHelper.areCertsDiff(currentRootCA, rootCA)) {
            rootCARotationType = CertRotationType.ServerCert;
          } else {
            rootCARotationType = CertRotationType.RootCert;
          }
          break;
        case CustomServerCert:
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "rootCA cannot be of type CustomServerCert.");
      }
    } else {
      // Consider this case:
      // node-to-node: false, client-to-node: true, rootAndClientRootCASame: true
      // Initial state: rootCA: UUID, clientRootCA: null
      // Request: { rootCA: null, clientRootCA: UUID, rootAndClientRootCASame: false }
      // Final state: rootCA: null, clientRootCA: UUID
      // In order to handle these kind of cases, resetting rootCA is necessary
      rootCA = null;
      if (isRootCARequired) {
        rootCA = currentRootCA;
      }
    }

    if (clientRootCA != null && !clientRootCA.equals(currentClientRootCA)) {
      // When the request comes to this block, this is when actual upgrade on clientRootCA
      // needs to be done. Now check on what kind of upgrade it is, RootCert or ServerCert
      CertificateInfo clientRootCert = CertificateInfo.get(clientRootCA);
      if (clientRootCert == null) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Certificate not present: " + rootCA);
      }

      switch (clientRootCert.certType) {
        case SelfSigned:
          clientRootCARotationType = CertRotationType.RootCert;
          break;
        case CustomCertHostPath:
          if (!userIntent.providerType.equals(CloudType.onprem)) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                "Certs of type CustomCertHostPath can only be used for on-prem universes.");
          }
          if (clientRootCert.getCustomCertInfo() == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "The certificate %s needs info. Update the cert and retry.",
                    clientRootCert.label));
          }
          if (currentClientRootCA != null
              && !CertificateHelper.areCertsDiff(currentClientRootCA, clientRootCA)) {
            clientRootCARotationType = CertRotationType.ServerCert;
          } else {
            clientRootCARotationType = CertRotationType.RootCert;
          }
          break;
        case CustomServerCert:
          if (clientRootCert.getCustomServerCertInfo() == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "The certificate %s needs info. Update the cert and retry.",
                    clientRootCert.label));
          }
          if (currentClientRootCA != null
              && !CertificateHelper.areCertsDiff(currentClientRootCA, clientRootCA)) {
            clientRootCARotationType = CertRotationType.ServerCert;
          } else {
            clientRootCARotationType = CertRotationType.RootCert;
          }
          break;
      }
    } else {
      // Consider this case:
      // node-to-node: false, client-to-node: true, rootAndClientRootCASame: false
      // Initial state: rootCA: null, clientRootCA: UUID
      // Request: { rootCA: UUID, clientRootCA: null, rootAndClientRootCASame: true }
      // Final state: rootCA: UUID, clientRootCA: null
      // In order to handle these kind of cases, resetting clientRootCA is necessary
      clientRootCA = null;
      if (isClientRootCARequired) {
        clientRootCA = currentClientRootCA;
      }
    }

    // When there is no upgrade needs to be done, fail the request
    if (rootCARotationType == CertRotationType.None
        && clientRootCARotationType == CertRotationType.None) {
      if (!(userIntent.enableNodeToNodeEncrypt
          && userIntent.enableClientToNodeEncrypt
          && !currentRootAndClientRootCASame
          && rootAndClientRootCASame)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "No changes in rootCA or clientRootCA.");
      }
    }
  }

  private void verifyParamsForKubernetesUpgrade(Universe universe) {
    if (rootCA == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "rootCA is null. Cannot perform any upgrade.");
    }

    if (clientRootCA != null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "clientRootCA not applicable for Kubernetes certificate rotation.");
    }

    if (rootAndClientRootCASame != null && !rootAndClientRootCASame) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "rootAndClientRootCASame cannot be false for Kubernetes universes.");
    }

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Certificate rotation for kubernetes universes cannot be Non-Rolling or Non-Restart.");
    }

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID currentRootCA = universe.getUniverseDetails().rootCA;

    if (!(userIntent.enableNodeToNodeEncrypt || userIntent.enableClientToNodeEncrypt)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Encryption-in-Transit is disabled for this universe. "
              + "Cannot perform certificate rotation.");
    }

    if (currentRootCA.equals(rootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Universe is already assigned to the provided rootCA: " + rootCA);
    }

    CertificateInfo rootCert = CertificateInfo.get(rootCA);
    if (rootCert == null) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Certificate not present: " + rootCA);
    }

    if (rootCert.certType != Type.SelfSigned) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Kubernetes universes supports only SelfSigned certificates.");
    }
  }

  public static class Converter extends BaseConverter<CertsRotateParams> {}
}

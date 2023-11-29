// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = CertsRotateParams.Converter.class)
@Slf4j
public class CertsRotateParams extends UpgradeTaskParams {

  public enum CertRotationType {
    None,
    ServerCert,
    RootCert
  }

  // If true, rotates server cert of rootCA
  public boolean selfSignedServerCertRotate = false;
  // If true, rotates server cert of clientRootCA
  public boolean selfSignedClientCertRotate = false;

  @JsonIgnore public CertRotationType rootCARotationType = CertRotationType.None;
  @JsonIgnore public CertRotationType clientRootCARotationType = CertRotationType.None;

  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    verifyCertificateValidity(universe);
    if (!userIntent.providerType.equals(CloudType.kubernetes)) {
      verifyParamsForNormalUpgrade(universe);
    } else {
      verifyParamsForKubernetesUpgrade(universe);
    }
  }

  private void verifyCertificateValidity(Universe universe) {
    boolean n2nCertExpired = CertificateHelper.checkNode2NodeCertsExpiry(universe);
    /*
     We will fail for cases -
     1. CA certs are rotated.
     2. Only Node to node certs are rotated.
    */
    if (n2nCertExpired && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      if (!selfSignedServerCertRotate && selfSignedClientCertRotate) {
        return;
      }
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Your node-to-node certificates have expired, so a rolling upgrade will not work. Retry"
              + " using the non-rolling option at a suitable time");
    }
  }

  private void verifyParamsForNormalUpgrade(Universe universe) {
    // Validate request params on different constraints based on current universe state.
    // Update rootCA, clientRootCA and rootAndClientRootCASame to their desired final state.
    // Decide what kind of upgrade needs to be done on rootCA and clientRootCA.

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    UUID currentRootCA = universeDetails.rootCA;
    UUID currentClientRootCA = universeDetails.clientRootCA;
    boolean currentRootAndClientRootCASame = universeDetails.rootAndClientRootCASame;

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Cert upgrade cannot be non restart.");
    }

    // Make sure rootCA and clientRootCA respects the rootAndClientRootCASame property
    if (rootAndClientRootCASame
        && rootCA != null
        && clientRootCA != null
        && !rootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "RootCA and ClientRootCA cannot be different when rootAndClientRootCASame is true.");
    }

    if (rootAndClientRootCASame && !userIntent.enableClientToNodeEncrypt) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "'Client to Node' encryption should be enabled before setting "
              + "rootAndClientRootCASame as true.");
    }

    boolean isRootCARequired =
        EncryptionInTransitUtil.isRootCARequired(
            userIntent.enableNodeToNodeEncrypt,
            userIntent.enableClientToNodeEncrypt,
            rootAndClientRootCASame);
    boolean isClientRootCARequired =
        EncryptionInTransitUtil.isClientRootCARequired(
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

      switch (rootCert.getCertType()) {
        case SelfSigned:
          rootCARotationType = CertRotationType.RootCert;
          break;
        case CustomCertHostPath:
          if (!userIntent.providerType.equals(CloudType.onprem)) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                "Certs of type CustomCertHostPath can only be used for on-prem universes.");
          }
          if (rootCert.getCustomCertPathParams() == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "The certificate %s needs info. Update the cert and retry.",
                    rootCert.getLabel()));
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
        case HashicorpVault:
          {
            rootCARotationType = CertRotationType.RootCert;
            break;
          }
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
        CertificateInfo rootCert = CertificateInfo.get(rootCA);
        if (selfSignedServerCertRotate && rootCert.getCertType() == CertConfigType.SelfSigned) {
          rootCARotationType = CertRotationType.ServerCert;
        }
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

      switch (clientRootCert.getCertType()) {
        case SelfSigned:
          clientRootCARotationType = CertRotationType.RootCert;
          break;
        case CustomCertHostPath:
          if (!userIntent.providerType.equals(CloudType.onprem)) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                "Certs of type CustomCertHostPath can only be used for on-prem universes.");
          }
          if (clientRootCert.getCustomCertPathParams() == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "The certificate %s needs info. Update the cert and retry.",
                    clientRootCert.getLabel()));
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
                    clientRootCert.getLabel()));
          }
          if (currentClientRootCA != null
              && !CertificateHelper.areCertsDiff(currentClientRootCA, clientRootCA)) {
            clientRootCARotationType = CertRotationType.ServerCert;
          } else {
            clientRootCARotationType = CertRotationType.RootCert;
          }
          break;
        case HashicorpVault:
          {
            clientRootCARotationType = CertRotationType.RootCert;
            break;
          }
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
        CertificateInfo clientRootCert = CertificateInfo.get(clientRootCA);
        if (selfSignedClientCertRotate
            && clientRootCert.getCertType() == CertConfigType.SelfSigned) {
          clientRootCARotationType = CertRotationType.ServerCert;
        }
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

    // clientRootCA will always be populated for 'hot cert reload' feature
    // just that it should not be different from rootCA in k8s universes
    if (clientRootCA != null && !rootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "clientRootCA not applicable for Kubernetes certificate rotation.");
    }

    if (!rootAndClientRootCASame) {
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

    if (!(rootCert.getCertType() == CertConfigType.SelfSigned
        || rootCert.getCertType() == CertConfigType.HashicorpVault)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Kubernetes universes supports only SelfSigned or HashicorpVault certificates.");
    }
  }

  public static CertsRotateParams mergeUniverseDetails(
      TlsConfigUpdateParams original, UniverseDefinitionTaskParams univDetails) {

    // TODO: fix this in a more general way so that we don't have to keep this in sync
    // with new fields defined in these methods.
    ObjectNode node = JsonNodeFactory.instance.objectNode();
    node.put("rootCA", (original.rootCA != null) ? original.rootCA.toString() : null);
    node.put(
        "clientRootCA", (original.clientRootCA != null) ? original.clientRootCA.toString() : null);
    node.put("selfSignedServerCertRotate", original.selfSignedServerCertRotate);
    node.put("selfSignedClientCertRotate", original.selfSignedClientCertRotate);
    node.put("rootAndClientRootCASame", original.rootAndClientRootCASame);

    // UpgradeOption needs special handling because it has a JsonProperty
    node.put("upgradeOption", Json.toJson(original.upgradeOption).asText());

    node.put("sleepAfterMasterRestartMillis", original.sleepAfterMasterRestartMillis);
    node.put("sleepAfterTServerRestartMillis", original.sleepAfterTServerRestartMillis);

    JsonNode universeDetailsJson = Json.toJson(univDetails);
    CommonUtils.deepMerge(universeDetailsJson, Json.toJson(node));

    return Json.fromJson(universeDetailsJson, CertsRotateParams.class);
  }

  public static class Converter extends BaseConverter<CertsRotateParams> {}
}

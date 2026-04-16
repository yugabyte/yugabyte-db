// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModelProperty;
import java.util.Objects;
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

  @ApiModelProperty(hidden = true)
  public CertRotationType rootCARotationType = CertRotationType.None;

  @ApiModelProperty(hidden = true)
  public CertRotationType clientRootCARotationType = CertRotationType.None;

  // Transient fields to track if rootCA/clientRootCA were explicitly set to null in the request
  // These are used to preserve null values during binding/merging
  @com.fasterxml.jackson.annotation.JsonIgnore
  public transient boolean rootCAExplicitlyNull = false;

  @com.fasterxml.jackson.annotation.JsonIgnore
  public transient boolean clientRootCAExplicitlyNull = false;

  /**
   * Minimum DB version for which hot cert reload is supported when only client-to-node (C2N)
   * encryption is enabled. For DB < this version with only C2N enabled, hot cert reload is
   * disabled.
   */
  public static final String HOT_CERT_RELOAD_C2N_ONLY_MIN_VERSION = "2025.2.1.0-b0";

  /**
   * Returns true if hot cert reload is supported from a C2N/version perspective. Returns false when
   * only client-to-node encryption is enabled and DB version is below 2025.2.1 (hot cert reload for
   * C2N-only is available from DB >= 2025.2.1).
   */
  public static boolean isHotCertReloadSupportedForUniverse(
      String ybSoftwareVersion,
      boolean enableClientToNodeEncrypt,
      boolean enableNodeToNodeEncrypt) {
    if (!enableClientToNodeEncrypt || enableNodeToNodeEncrypt) {
      return true;
    }
    return Util.compareYbVersions(ybSoftwareVersion, HOT_CERT_RELOAD_C2N_ONLY_MIN_VERSION, true)
        >= 0;
  }

  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    verifyCertificateValidity(universe);
    if (!userIntent.providerType.equals(CloudType.kubernetes)) {
      verifyParamsForNormalUpgrade(universe, isFirstTry);
    } else {
      // TODO: Fix rotate certs api for VM universes and add this validation for VM universes.
      commonValidation(universe);
      verifyParamsForKubernetesUpgrade(universe, isFirstTry);
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

  private void verifyNonRestartUpgradeSupport(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    String softwareVersion = userIntent.ybSoftwareVersion;

    // Check if YB version supports cert reload (>= 2.14.0.0-b1)
    if (Util.compareYbVersions(softwareVersion, "2.14.0.0-b1", true) < 0) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Non-restart certificate rotation is not supported for universe with software version: "
              + softwareVersion
              + ". Minimum required version is 2.14.0.0-b1.");
    }

    // Check if cert reload feature flag is enabled
    if (runtimeConfGetter == null) {
      runtimeConfGetter = StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
    }
    boolean featureFlagEnabled = runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableCertReload);
    if (!featureFlagEnabled) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Non-restart certificate rotation requires the cert reload feature to be enabled. "
              + "Please enable the feature flag 'yb.features.cert_reload.enabled' and retry.");
    }

    // Check if universe is configured for cert reload
    boolean universeConfigured =
        Boolean.parseBoolean(
            universe
                .getConfig()
                .getOrDefault(Universe.KEY_CERT_HOT_RELOADABLE, Boolean.FALSE.toString()));
    if (!universeConfigured) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Non-restart certificate rotation requires the universe to be configured for cert "
              + "reload. The universe will be automatically configured during the first cert "
              + "rotation, but for non-restart upgrades, it must be configured beforehand. "
              + "Please perform a rolling cert rotation first to configure the universe.");
    }

    // Check if node-to-node certs are expired (non-restart upgrade cannot proceed if expired)
    boolean n2nCertExpired = CertificateHelper.checkNode2NodeCertsExpiry(universe);
    if (n2nCertExpired) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Non-restart certificate rotation cannot be performed when node-to-node certificates "
              + "have expired. Please use rolling or non-rolling upgrade option instead.");
    }

    // Hot cert reload for client-to-node-only is supported from DB >= 2025.2.1
    if (!isHotCertReloadSupportedForUniverse(
        softwareVersion,
        userIntent.enableClientToNodeEncrypt,
        userIntent.enableNodeToNodeEncrypt)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Non-restart certificate rotation is not supported for client-to-node-only universes "
              + "with DB version below "
              + HOT_CERT_RELOAD_C2N_ONLY_MIN_VERSION
              + ". Please use rolling or non-rolling upgrade, or upgrade DB to "
              + HOT_CERT_RELOAD_C2N_ONLY_MIN_VERSION
              + " or later.");
    }
  }

  private void commonValidation(Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    if (!userIntent.enableClientToNodeEncrypt
        && !userIntent.enableNodeToNodeEncrypt
        && (rootCA != null || clientRootCA != null)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot rotate rootCA or clientRootCA when encryption-in-transit is disabled.");
    }
    if (!userIntent.enableClientToNodeEncrypt && selfSignedClientCertRotate) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot rotate client certificate when client to node encryption is disabled.");
    }
    if (!userIntent.enableNodeToNodeEncrypt && selfSignedServerCertRotate) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot rotate server certificate when node to node encryption is disabled.");
    }
  }

  private void verifyParamsForNormalUpgrade(Universe universe, boolean isFirstTry) {
    // Validate request params on different constraints based on current universe state.
    // Update rootCA, clientRootCA and rootAndClientRootCASame to their desired final state.
    // Decide what kind of upgrade needs to be done on rootCA and clientRootCA.

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    UUID currentRootCA = universeDetails.rootCA;
    UUID currentClientRootCA = universeDetails.clientRootCA;

    // Validate non-restart upgrade option for non-Kubernetes universes
    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      verifyNonRestartUpgradeSupport(universe);
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

    if (isFirstTry) {
      // TODO Move this out of verify params. This pattern is in many other places too.
      setAdditionalTaskParams(universe);
    }
  }

  // Sets additional tasks params which are derived based on the universe fields.
  private void setAdditionalTaskParams(Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    UUID currentRootCA = universeDetails.rootCA;
    UUID currentClientRootCA = universeDetails.clientRootCA;
    boolean currentRootAndClientRootCASame = universeDetails.rootAndClientRootCASame;
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
          rootCARotationType = CertRotationType.RootCert;
          break;
        case K8SCertManager:
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              "K8SCertManager certificates are not supported for VM/universe certificate"
                  + " rotation.");
        default:
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "Unsupported certificate type: " + rootCert.getCertType());
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
        if (rootCA != null) {
          CertificateInfo rootCert = CertificateInfo.get(rootCA);
          if (rootCert != null
              && selfSignedServerCertRotate
              && rootCert.getCertType() == CertConfigType.SelfSigned) {
            rootCARotationType = CertRotationType.ServerCert;
          }
        }
      }
    }

    if (clientRootCA != null && !clientRootCA.equals(currentClientRootCA)) {
      // When the request comes to this block, this is when actual upgrade on clientRootCA
      // needs to be done. Now check on what kind of upgrade it is, RootCert or ServerCert
      CertificateInfo clientRootCert = CertificateInfo.get(clientRootCA);
      if (clientRootCert == null) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Certificate not present: " + clientRootCA);
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
          clientRootCARotationType = CertRotationType.RootCert;
          break;
        case K8SCertManager:
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              "K8SCertManager certificates are not supported for VM/universe certificate"
                  + " rotation.");
        default:
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "Unsupported certificate type: " + clientRootCert.getCertType());
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
        if (clientRootCA != null) {
          CertificateInfo clientRootCert = CertificateInfo.get(clientRootCA);
          if (clientRootCert != null
              && selfSignedClientCertRotate
              && clientRootCert.getCertType() == CertConfigType.SelfSigned) {
            clientRootCARotationType = CertRotationType.ServerCert;
          }
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

  private void verifyParamsForKubernetesUpgrade(Universe universe, boolean isFirstTry) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.enableNodeToNodeEncrypt && rootCA == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "rootCA is null. Cannot perform any upgrade.");
    }

    if (userIntent.enableClientToNodeEncrypt && rootCA == null && clientRootCA == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "rootCA or clientRootCA is null. Cannot perform any upgrade when client to node"
              + " encryption is enabled.");
    }

    // clientRootCA will always be populated for 'hot cert reload' feature
    // just that it should not be different from rootCA in k8s universes
    if (clientRootCA != null && rootCA != null && !rootCA.equals(clientRootCA)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "rootCA and clientRootCA cannot be different for Kubernetes certificate rotation.");
    }

    if (!rootAndClientRootCASame) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "rootAndClientRootCASame cannot be false for Kubernetes universes.");
    }

    UUID currentRootCA = universe.getUniverseDetails().rootCA;
    UUID currentClientRootCA = universe.getUniverseDetails().clientRootCA;

    // Check if certs are managed through Kubernetes cert manager
    if (currentRootCA != null) {
      CertificateInfo certInfo = CertificateInfo.get(currentRootCA);
      if (certInfo != null && certInfo.getCertType() == CertConfigType.K8SCertManager) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Certificate rotation is not supported for Kubernetes cert manager managed"
                + " certificates.");
      }
    }

    if (currentClientRootCA != null) {
      CertificateInfo certInfo = CertificateInfo.get(currentClientRootCA);
      if (certInfo != null && certInfo.getCertType() == CertConfigType.K8SCertManager) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Certificate rotation is not supported for Kubernetes cert manager managed"
                + " certificates.");
      }
    }

    if (rootCA != null) {
      CertificateInfo newCertInfo = CertificateInfo.get(rootCA);
      if (newCertInfo != null && newCertInfo.getCertType() == CertConfigType.K8SCertManager) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Certificate rotation is not supported for Kubernetes cert manager managed"
                + " certificates.");
      }
    }

    if (clientRootCA != null) {
      CertificateInfo newClientCertInfo = CertificateInfo.get(clientRootCA);
      if (newClientCertInfo != null
          && newClientCertInfo.getCertType() == CertConfigType.K8SCertManager) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Certificate rotation is not supported for Kubernetes cert manager managed"
                + " certificates.");
      }
    }

    // Allow non-restart upgrade for Kubernetes universes if cert reload is supported
    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      String softwareVersion = userIntent.ybSoftwareVersion;
      if (Util.compareYBVersions(softwareVersion, "2025.2.0.0-b0", "2.27.0.0-b0", true) < 0) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Non-restart certificate rotation is not supported for Kubernetes universe with"
                + " software version: "
                + softwareVersion);
      }
    }

    // Check if rootCA or clientRootCA has changed
    boolean changeInRootCA = !Objects.equals(currentRootCA, rootCA);

    boolean changeInClientRootCA = !Objects.equals(currentClientRootCA, clientRootCA);

    if (!changeInRootCA
        && !changeInClientRootCA
        && !selfSignedServerCertRotate
        && !selfSignedClientCertRotate) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "No changes in rootCA or server certificate rotation has been requested.");
    }

    if (rootCA != null) {
      CertificateInfo rootCert = CertificateInfo.get(rootCA);
      if (rootCert == null) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Certificate not present: " + rootCA);
      }
      if (!(rootCert.getCertType() == CertConfigType.SelfSigned
          || rootCert.getCertType() == CertConfigType.HashicorpVault)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Kubernetes universes supports only SelfSigned or HashicorpVault certificates.");
      }
    }

    if (clientRootCA != null) {
      CertificateInfo clientRootCert = CertificateInfo.get(clientRootCA);
      if (clientRootCert == null) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Certificate not present: " + clientRootCA);
      }
      if (!(clientRootCert.getCertType() == CertConfigType.SelfSigned
          || clientRootCert.getCertType() == CertConfigType.HashicorpVault)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Kubernetes universes supports only SelfSigned or HashicorpVault certificates.");
      }
    }

    if (isFirstTry) {
      // Set additional task parameters for Kubernetes universes
      setAdditionalTaskParamsForKubernetes(universe);
    }
  }

  // Sets additional task params for Kubernetes universes
  private void setAdditionalTaskParamsForKubernetes(Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    UUID currentRootCA = universeDetails.rootCA;
    UUID currentClientRootCA = universeDetails.clientRootCA;
    if ((rootCA != null && !rootCA.equals(currentRootCA))
        || (clientRootCA != null && !clientRootCA.equals(currentClientRootCA))) {
      rootCARotationType = CertRotationType.RootCert;
    } else if (selfSignedServerCertRotate && selfSignedClientCertRotate) {
      rootCARotationType = CertRotationType.ServerCert;
    } else if (selfSignedServerCertRotate || selfSignedClientCertRotate) {
      rootCARotationType = CertRotationType.ServerCert;
      if (!selfSignedClientCertRotate && userIntent.enableClientToNodeEncrypt) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Cannot rotate only node to node certificate when client to node encryption is"
                + " enabled.");
      } else if (!selfSignedServerCertRotate && userIntent.enableNodeToNodeEncrypt) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Cannot rotate only client to node certificate when node to node encryption is"
                + " enabled.");
      }
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

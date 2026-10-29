// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;

/**
 * Reads the client cert the YugabyteDB chart has cert-manager issue for a universe. YBA holds no
 * key for a K8SCertManager CA, so it cannot sign a client cert itself.
 */
@Singleton
public class CertManagerClientCert {

  private static final String SECRET_CERT_KEY = "tls.crt";
  private static final String SECRET_KEY_KEY = "tls.key";
  private static final String OLD_NAMING_SECRET_NAME = "yugabyte-tls-client-cert";
  private static final String KEY_ALGORITHM_OVERRIDE = "tls.certManager.certificates.algorithm";

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public CertManagerClientCert(KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  /** The PEM cert and key in the universe's client cert Secret. */
  public Pair<String, String> read(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    // Every AZ's Secret is signed by the same CA, so the first one is enough.
    Map.Entry<UUID, Map<String, String>> az =
        KubernetesUtil.getConfigPerAZ(primary.placementInfo).entrySet().iterator().next();
    String secretName = getSecretName(universe, az.getKey());
    Map<String, String> data = Collections.emptyMap();
    try {
      Secret secret =
          kubernetesManagerFactory
              .getManager()
              .getSecret(az.getValue(), secretName, getNamespace(universe, az));
      if (secret != null && secret.getData() != null) {
        data = secret.getData();
      }
    } catch (RuntimeException e) {
      // The shell manager throws on a missing Secret, the native one returns null.
      if (!StringUtils.contains(e.getMessage(), "Error from server (NotFound): secrets")) {
        throw e;
      }
    }
    String cert = data.get(SECRET_CERT_KEY);
    String key = data.get(SECRET_KEY_KEY);
    if (StringUtils.isAnyBlank(cert, key)) {
      throw new IllegalStateException("Client cert Secret " + secretName + " is not ready");
    }
    Base64.Decoder decoder = Base64.getDecoder();
    return Pair.of(new String(decoder.decode(cert)), new String(decoder.decode(key)));
  }

  /**
   * Rejects helm overrides that make the chart issue a non-RSA client key for a universe that needs
   * one: the YB Java client only loads RSA keys.
   */
  public static void validateHelmOverrides(
      UniverseDefinitionTaskParams details,
      @Nullable String universeOverrides,
      @Nullable Map<String, String> azOverrides) {
    if (!RpcClientCert.isClientCertRequired(details, details.rootCA)) {
      return;
    }
    List<String> overrides = new ArrayList<>();
    overrides.add(universeOverrides);
    if (azOverrides != null) {
      overrides.addAll(azOverrides.values());
    }
    for (String overridesYaml : overrides) {
      String algorithm;
      try {
        algorithm =
            HelmUtils.flattenMap(HelmUtils.convertYamlToMap(overridesYaml))
                .get(KEY_ALGORITHM_OVERRIDE);
      } catch (Exception e) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Kubernetes overrides are not valid YAML: " + e.getMessage());
      }
      if (algorithm != null && !RpcClientCert.SUPPORTED_KEY_ALGORITHM.equalsIgnoreCase(algorithm)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s=%s is not supported with %s: YugabyteDB Anywhere can only present an %s client"
                    + " certificate",
                KEY_ALGORITHM_OVERRIDE,
                algorithm,
                RpcClientCert.USE_CLIENT_CERTS_GFLAG,
                RpcClientCert.SUPPORTED_KEY_ALGORITHM));
      }
    }
  }

  @VisibleForTesting
  static String getSecretName(Universe universe, UUID azUuid) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if (!details.useNewHelmNamingStyle) {
      return OLD_NAMING_SECRET_NAME;
    }
    String releaseName =
        KubernetesUtil.getHelmReleaseName(
            isMultiAz(universe),
            details.nodePrefix,
            universe.getName(),
            AvailabilityZone.getOrBadRequest(azUuid).getCode(),
            false /* isReadOnlyCluster */,
            true /* newNamingStyle */);
    return KubernetesUtil.getHelmFullName(releaseName) + "-client-tls";
  }

  private static String getNamespace(Universe universe, Map.Entry<UUID, Map<String, String>> az) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    return KubernetesUtil.getKubernetesNamespace(
        isMultiAz(universe),
        details.nodePrefix,
        AvailabilityZone.getOrBadRequest(az.getKey()).getCode(),
        az.getValue(),
        details.useNewHelmNamingStyle,
        false /* isReadOnlyCluster */);
  }

  private static boolean isMultiAz(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    return PlacementInfoUtil.isMultiAZ(
        Provider.getOrBadRequest(UUID.fromString(primary.userIntent.provider)));
  }
}

// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import java.time.Duration;
import java.util.Base64;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Reads the client cert the YugabyteDB chart has cert-manager issue for a universe. YBA holds no
 * key for a K8SCertManager CA, so it cannot sign a client cert itself.
 */
@Slf4j
@Singleton
public class CertManagerClientCert {

  private static final String SECRET_CERT_KEY = "tls.crt";
  private static final String SECRET_KEY_KEY = "tls.key";
  private static final String OLD_NAMING_SECRET_NAME = "yugabyte-tls-client-cert";
  @VisibleForTesting static final int READ_ATTEMPTS = 3;
  private static final Duration READ_RETRY_DELAY = Duration.ofMillis(500);

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public CertManagerClientCert(KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  /** The PEM cert and key in the universe's client cert Secret. */
  public RpcClientCert.CertAndKey read(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    // Every AZ's Secret is signed by the same CA, so the first one is enough.
    Map.Entry<UUID, Map<String, String>> az =
        KubernetesUtil.getConfigPerAZ(primary.placementInfo).entrySet().iterator().next();
    String secretName = getSecretName(universe, az.getKey());
    Map<String, String> data = Collections.emptyMap();
    Secret secret = getSecret(az.getValue(), secretName, getNamespace(universe, az));
    if (secret != null && secret.getData() != null) {
      data = secret.getData();
    }
    String cert = data.get(SECRET_CERT_KEY);
    String key = data.get(SECRET_KEY_KEY);
    if (StringUtils.isAnyBlank(cert, key)) {
      throw new IllegalStateException("Client cert Secret " + secretName + " is not ready");
    }
    Base64.Decoder decoder = Base64.getDecoder();
    return new RpcClientCert.CertAndKey(
        new String(decoder.decode(cert)), new String(decoder.decode(key)));
  }

  // Retries API errors a few times, so one blip doesn't leave a client without the cert; a missing
  // Secret isn't retried, since WaitForRpcClientCert waits for cert-manager to issue it.
  private Secret getSecret(Map<String, String> config, String name, String namespace) {
    for (int attempt = 1; ; attempt++) {
      try {
        return kubernetesManagerFactory.getManager().getSecret(config, name, namespace);
      } catch (RuntimeException e) {
        // The shell manager throws on a missing Secret, the native one returns null.
        if (StringUtils.contains(e.getMessage(), "Error from server (NotFound): secrets")) {
          return null;
        }
        if (attempt == READ_ATTEMPTS) {
          throw e;
        }
        log.warn("Reading Secret {} failed, retrying: {}", name, e.getMessage());
        try {
          Thread.sleep(READ_RETRY_DELAY.toMillis());
        } catch (InterruptedException ie) {
          Thread.currentThread().interrupt();
          throw e;
        }
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

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.CertificateInfo;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.YBCertificate;
import io.yugabyte.operator.v1alpha1.YBCertificateStatus;
import java.io.StringReader;
import java.io.StringWriter;
import java.security.PrivateKey;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.bouncycastle.asn1.pkcs.PrivateKeyInfo;
import org.bouncycastle.openssl.PEMKeyPair;
import org.bouncycastle.openssl.PEMParser;
import org.bouncycastle.openssl.jcajce.JcaPEMKeyConverter;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;

@Slf4j
public class YBCertificateReconciler implements ResourceEventHandler<YBCertificate>, Runnable {

  // Constants
  private static final String CERT_TYPE_SELF_SIGNED = "SELF_SIGNED";
  private static final String CERT_TYPE_K8S_CERT_MANAGER = "K8S_CERT_MANAGER";
  private static final String SECRET_KEY_CERT = "ca.crt";
  private static final String SECRET_KEY_PRIVATE_KEY = "ca.key";
  private static final String STORAGE_PATH_CONFIG = "yb.storage.path";

  // Fields
  private final SharedIndexInformer<YBCertificate> informer;
  private final Lister<YBCertificate> lister;
  private final MixedOperation<
          YBCertificate, KubernetesResourceList<YBCertificate>, Resource<YBCertificate>>
      resourceClient;
  private final String namespace;
  private final OperatorUtils operatorUtils;
  private final RuntimeConfGetter runtimeConfGetter;

  private final ResourceTracker resourceTracker = new ResourceTracker();

  // The current certificate resource being reconciled, for associating secret dependencies.
  private KubernetesResourceDetails currentReconcileResource;

  public Set<KubernetesResourceDetails> getTrackedResources() {
    return resourceTracker.getTrackedResources();
  }

  public ResourceTracker getResourceTracker() {
    return resourceTracker;
  }

  public YBCertificateReconciler(
      SharedIndexInformer<YBCertificate> cmInformer,
      MixedOperation<YBCertificate, KubernetesResourceList<YBCertificate>, Resource<YBCertificate>>
          resourceClient,
      String namespace,
      OperatorUtils operatorUtils,
      RuntimeConfGetter runtimeConfGetter) {
    this.resourceClient = resourceClient;
    this.informer = cmInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.namespace = namespace;
    this.operatorUtils = operatorUtils;
    this.runtimeConfGetter = runtimeConfGetter;
  }

  /**
   * Update the status of a YBCertificate resource
   *
   * @param certificate the YBCertificate resource to update
   * @param success whether the operation was successful
   * @param configUUID the certificate configuration UUID
   * @param message the status message
   */
  private void updateStatus(
      YBCertificate certificate, boolean success, String configUUID, String message) {
    YBCertificateStatus status = certificate.getStatus();
    if (status == null) {
      status = new YBCertificateStatus();
    }
    status.setSuccess(success);
    status.setMessage(message);
    status.setResourceUUID(configUUID);
    certificate.setStatus(status);
    resourceClient.inNamespace(namespace).resource(certificate).replaceStatus();
  }

  @Override
  public void onAdd(YBCertificate certificate) {
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(certificate);
    resourceTracker.trackResource(certificate);
    currentReconcileResource = resourceDetails;
    log.trace("Tracking resource {}, all tracked: {}", resourceDetails, getTrackedResources());

    log.info("Adding YBCertificate: {}", certificate.getMetadata().getName());

    try {
      processCertificateCreation(certificate);
    } catch (Exception e) {
      log.error(
          "Failed to process YBCertificate {}: {}",
          certificate.getMetadata().getName(),
          e.getMessage());
      updateStatus(certificate, false, "", "Failed to process certificate: " + e.getMessage());
    }
  }

  private void processCertificateCreation(YBCertificate certificate) throws Exception {

    // Check if already processed
    if (isAlreadyProcessed(certificate)) {
      log.info("YBCertificate {} already processed, skipping", certificate.getMetadata().getName());
      return;
    }

    String cuuid = operatorUtils.getCustomerUUID();

    // Create the certificate
    String configUUID = createCertificate(certificate, cuuid);

    updateStatus(certificate, true, configUUID, "Certificate created successfully");
    log.info(
        "Successfully created YBCertificate {} with UUID: {}",
        certificate.getMetadata().getName(),
        configUUID);
  }

  /**
   * Check if certificate has already been processed by querying YBA database
   *
   * @param certificate the YBCertificate resource to check
   * @return true if already processed, false otherwise
   */
  private boolean isAlreadyProcessed(YBCertificate certificate) {
    try {
      String cuuid = operatorUtils.getCustomerUUID();
      UUID customerUUID = UUID.fromString(cuuid);
      String certificateName = certificate.getMetadata().getName();

      // Query YBA database to check if certificate with this label already exists
      CertificateInfo existingCert = CertificateInfo.get(customerUUID, certificateName);
      return existingCert != null;
    } catch (Exception e) {
      log.warn(
          "Failed to check if certificate {} is already processed: {}",
          certificate.getMetadata().getName(),
          e.getMessage());
      // If we can't query the database, fall back to checking the CR status
      return certificate.getStatus() != null && certificate.getStatus().getResourceUUID() != null;
    }
  }

  /**
   * Create certificate based on type
   *
   * @param certificate the YBCertificate resource
   * @param cuuid the customer UUID
   * @return the created certificate UUID
   * @throws Exception if certificate creation fails
   */
  private String createCertificate(YBCertificate certificate, String cuuid) throws Exception {
    String configName = certificate.getMetadata().getName();
    String certType = certificate.getSpec().getCertType().getValue();

    // Read certificate content from secret
    String rootCertificate = getCertificateContent(certificate);
    validateCertificateContent(rootCertificate);

    // Create certificate based on type
    if (CERT_TYPE_SELF_SIGNED.equals(certType)) {
      return createSelfSignedCertificate(certificate, cuuid, configName, rootCertificate);
    } else if (CERT_TYPE_K8S_CERT_MANAGER.equals(certType)) {
      return createK8sCertManagerCertificate(certificate, cuuid, configName, rootCertificate);
    } else {
      // This should never happen due to validation above
      log.error("Unsupported certificate type: {}", certType);
      updateStatus(certificate, false, "", "Unsupported certificate type: " + certType);
      throw new IllegalArgumentException("Unsupported certificate type: " + certType);
    }
  }

  /**
   * Validate certificate content is not empty
   *
   * @param rootCertificate the certificate content to validate
   * @throws IllegalArgumentException if certificate content is null or empty
   */
  private void validateCertificateContent(String rootCertificate) {
    if (rootCertificate == null || rootCertificate.trim().isEmpty()) {
      throw new IllegalArgumentException(
          "Root certificate content not found in secret (expected '" + SECRET_KEY_CERT + "')");
    }
  }

  /**
   * Create self-signed certificate
   *
   * @param certificate the YBCertificate resource
   * @param cuuid the customer UUID
   * @param configName the certificate configuration name
   * @param rootCertificate the root certificate content
   * @return the created certificate UUID
   * @throws Exception if certificate creation fails
   */
  private String createSelfSignedCertificate(
      YBCertificate certificate, String cuuid, String configName, String rootCertificate)
      throws Exception {
    String key = getKeyContent(certificate);
    if (key == null || key.trim().isEmpty()) {
      throw new IllegalArgumentException(
          "Private key content not found in secret. SELF_SIGNED certificates require both '"
              + SECRET_KEY_CERT
              + "' and '"
              + SECRET_KEY_PRIVATE_KEY
              + "'");
    }

    return CertificateHelper.uploadRootCA(
            configName,
            UUID.fromString(cuuid),
            runtimeConfGetter.getStaticConf().getString(STORAGE_PATH_CONFIG),
            rootCertificate,
            key,
            CertConfigType.SelfSigned,
            null,
            null,
            true)
        .toString();
  }

  /**
   * Create K8s cert manager certificate
   *
   * @param certificate the YBCertificate resource
   * @param cuuid the customer UUID
   * @param configName the certificate configuration name
   * @param rootCertificate the root certificate content
   * @return the created certificate UUID
   * @throws Exception if certificate creation fails
   */
  private String createK8sCertManagerCertificate(
      YBCertificate certificate, String cuuid, String configName, String rootCertificate)
      throws Exception {
    return CertificateHelper.uploadRootCA(
            configName,
            UUID.fromString(cuuid),
            runtimeConfGetter.getStaticConf().getString(STORAGE_PATH_CONFIG),
            rootCertificate,
            null,
            CertConfigType.K8SCertManager,
            null,
            null,
            true)
        .toString();
  }

  @Override
  public void onUpdate(YBCertificate oldCertificate, YBCertificate newCertificate) {
    log.info("Updating YBCertificate: {}", newCertificate.getMetadata().getName());
    // Persist the latest resource YAML so the OperatorResource table stays current.
    resourceTracker.trackResource(newCertificate);

    try {
      processCertificateCreation(newCertificate);
    } catch (Exception e) {
      log.error(
          "Failed to process YBCertificate update {}: {}",
          newCertificate.getMetadata().getName(),
          e.getMessage());
      updateStatus(
          newCertificate, false, "", "Failed to process certificate update: " + e.getMessage());
    }
  }

  @Override
  public void onDelete(YBCertificate certificate, boolean deletedFinalStateUnknown) {
    log.info("Deleting YBCertificate: {}", certificate.getMetadata().getName());
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(certificate);
    Set<KubernetesResourceDetails> orphaned = resourceTracker.untrackResource(resourceDetails);
    log.info("Untracked certificate {} and orphaned dependencies: {}", resourceDetails, orphaned);

    try {
      processCertificateDeletion(certificate);
    } catch (Exception e) {
      log.error(
          "Failed to delete YBCertificate {}: {}",
          certificate.getMetadata().getName(),
          e.getMessage());
    }
  }

  /**
   * Main logic for processing certificate deletion
   *
   * @param certificate the YBCertificate resource to delete
   * @throws Exception if deletion processing fails
   */
  private void processCertificateDeletion(YBCertificate certificate) throws Exception {
    String cuuid = operatorUtils.getCustomerUUID();
    String configUUID =
        certificate.getStatus() != null ? certificate.getStatus().getResourceUUID() : null;
    String configName = certificate.getMetadata().getName();

    if (configUUID != null) {
      deleteCertificateByUUID(configUUID, configName, cuuid);
    } else {
      deleteCertificateByName(configName, cuuid);
    }
  }

  /**
   * Delete certificate by UUID with fallback to name-based deletion
   *
   * @param configUUID the certificate configuration UUID
   * @param configName the certificate configuration name
   * @param cuuid the customer UUID
   */
  private void deleteCertificateByUUID(String configUUID, String configName, String cuuid) {
    try {
      CertificateInfo.delete(UUID.fromString(configUUID), UUID.fromString(cuuid));
      log.info("Successfully deleted YBCertificate {} with UUID: {}", configName, configUUID);
    } catch (Exception e) {
      if (e.getMessage() != null && e.getMessage().contains("Invalid Cert ID")) {
        log.warn(
            "Certificate UUID {} not found, trying to delete by name: {}", configUUID, configName);
        deleteCertificateByName(configName, cuuid);
      } else {
        log.error("Failed to delete certificate {}: {}", configUUID, e.getMessage());
      }
    }
  }

  /**
   * Delete certificate by name
   *
   * @param configName the certificate configuration name
   * @param cuuid the customer UUID
   */
  private void deleteCertificateByName(String configName, String cuuid) {
    try {
      CertificateInfo existingCert = CertificateInfo.get(UUID.fromString(cuuid), configName);
      if (existingCert != null) {
        CertificateInfo.delete(existingCert.getUuid(), UUID.fromString(cuuid));
        log.info("Successfully deleted certificate by name: {}", configName);
      }
    } catch (Exception e) {
      log.error("Failed to delete certificate by name {}: {}", configName, e.getMessage());
    }
  }

  // ==================== Secret Content Retrieval ====================

  /**
   * Get certificate content from Kubernetes secret
   *
   * @param certificate the YBCertificate resource
   * @return the certificate content or null if not found
   */
  private String getCertificateContent(YBCertificate certificate) {
    return getSecretContent(certificate, SECRET_KEY_CERT);
  }

  /**
   * Get private key content from Kubernetes secret with PKCS#8 to PKCS#1 conversion
   *
   * @param certificate the YBCertificate resource
   * @return the private key content or null if not found
   */
  private String getKeyContent(YBCertificate certificate) {
    String keyContent = getSecretContent(certificate, SECRET_KEY_PRIVATE_KEY);
    if (keyContent != null) {
      return convertKeyToPKCS1Format(keyContent);
    }
    return keyContent;
  }

  /**
   * Generic method to get content from Kubernetes secret
   *
   * @param certificate the YBCertificate resource
   * @param key the secret key to retrieve
   * @return the secret content or null if not found
   */
  private String getSecretContent(YBCertificate certificate, String key) {
    if (certificate.getSpec().getCertificateSecretRef() != null) {
      String secretName = certificate.getSpec().getCertificateSecretRef().getName();
      String secretNamespace = getSecretNamespace(certificate);
      return operatorUtils.getAndParseSecretForKey(
          secretName, secretNamespace, key, resourceTracker, currentReconcileResource);
    }
    return null;
  }

  /**
   * Get secret namespace, defaulting to certificate namespace if not specified
   *
   * @param certificate the YBCertificate resource
   * @return the secret namespace
   */
  private String getSecretNamespace(YBCertificate certificate) {
    String secretNamespace = certificate.getSpec().getCertificateSecretRef().getNamespace();
    return secretNamespace != null ? secretNamespace : certificate.getMetadata().getNamespace();
  }

  // ==================== Key Format Conversion ====================

  /**
   * Converts private key from PKCS#8 format to PKCS#1 format if needed.
   *
   * @param keyContent the private key content to convert
   * @return the converted key content or original if no conversion needed
   */
  private String convertKeyToPKCS1Format(String keyContent) {
    try {
      PEMParser parser = new PEMParser(new StringReader(keyContent));
      Object parsedObject = parser.readObject();

      if (parsedObject instanceof PEMKeyPair) {
        return keyContent;
      } else if (parsedObject instanceof PrivateKeyInfo) {
        log.debug("Converting PKCS#8 format private key to PKCS#1 format");
        PrivateKeyInfo privateKeyInfo = (PrivateKeyInfo) parsedObject;
        JcaPEMKeyConverter converter = new JcaPEMKeyConverter();
        PrivateKey privateKey = converter.getPrivateKey(privateKeyInfo);

        try (StringWriter stringWriter = new StringWriter();
            JcaPEMWriter pemWriter = new JcaPEMWriter(stringWriter)) {
          pemWriter.writeObject(privateKey);
          pemWriter.flush();
          return stringWriter.toString();
        }
      } else {
        log.warn(
            "Unknown private key format: {}",
            parsedObject != null ? parsedObject.getClass().getName() : "null");
        return keyContent;
      }
    } catch (Exception e) {
      log.error("Failed to convert private key format: {}", e.getMessage());
      return keyContent;
    }
  }

  // ==================== ResourceEventHandler Implementation ====================

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}

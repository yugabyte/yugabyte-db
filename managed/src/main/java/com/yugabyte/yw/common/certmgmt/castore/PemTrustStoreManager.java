// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.Iterators;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyStoreException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;

/** Relates to YBA's PEM trust store */
@Slf4j
@Singleton
public class PemTrustStoreManager implements TrustStoreManager {
  public static final String TRUSTSTORE_FILE_NAME = "yb-ca-bundle.pem";

  /** Creates a trust-store with only custom CA certificates in PEM format. */
  public boolean addCertificate(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    log.debug("Trying to update YBA's PEM truststore ...");

    List<Certificate> newCerts = null;
    newCerts = getX509Certificate(certPath);
    if (CollectionUtils.isEmpty(newCerts)) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("No new CA certificate exists at %s", certPath));
    }

    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    boolean doesTrustStoreExist = new File(trustStorePath).exists();
    if (!doesTrustStoreExist) {
      File sysTrustStoreFile = new File(trustStorePath);
      sysTrustStoreFile.createNewFile();
      log.debug("Created an empty YBA PEM trust-store");
    } else {
      List<Certificate> trustCerts = getCertsInTrustStore(trustStorePath, trustStorePassword);
      List<Certificate> addedCertChain = new ArrayList<Certificate>(newCerts);
      if (trustCerts != null) {
        // Check if such an alias already exists.
        for (int i = 0; i < addedCertChain.size(); i++) {
          Certificate newCert = addedCertChain.get(i);
          boolean exists = trustCerts.contains(newCert);
          if (!exists) {
            break;
          }
          // In case of certificate chain, we can have the same root/intermediate
          // cert present in the chain. Throw error in case all of these exists.
          if (exists && !suppressErrors && addedCertChain.size() - 1 == i) {
            String msg = "CA certificate with same content already exists";
            log.error(msg);
            throw new PlatformServiceException(BAD_REQUEST, msg);
          } else if (exists && addedCertChain.size() != i) {
            newCerts.remove(i);
          }
        }
      }
    }

    // Update the trust store in file system.
    List<X509Certificate> x509Certificates =
        newCerts.stream()
            .filter(certificate -> certificate instanceof X509Certificate)
            .map(certificate -> (X509Certificate) certificate)
            .collect(Collectors.toList());
    CertificateHelper.writeCertBundleToCertPath(x509Certificates, trustStorePath, false, true);
    log.debug("Truststore '{}' now has the cert {}", trustStorePath, certAlias);

    // Backup up YBA's PEM trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));

    log.info("Custom CA certificate {} added in PEM truststore", certAlias);
    return !doesTrustStoreExist;
  }

  public void replaceCertificate(
      String oldCertPath,
      String newCertPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws CertificateException, IOException {

    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    log.debug("Trying to replace cert {} in the PEM truststore {}..", certAlias, trustStorePath);
    List<Certificate> trustCerts = getCertificates(trustStorePath);

    // Check if such a cert already exists.
    List<Certificate> oldCerts = getX509Certificate(oldCertPath);
    boolean exists = false;
    for (Certificate oldCert : oldCerts) {
      if (isCertificateUsedInOtherChain(oldCert)) {
        log.debug("Certificate {} is part of a chain, skipping replacement.", certAlias);
        continue;
      }
      exists = trustCerts.remove(oldCert);
      if (!exists && !suppressErrors) {
        String msg = String.format("Certificate '%s' doesn't exist to update", certAlias);
        log.error(msg);
        throw new PlatformServiceException(BAD_REQUEST, msg);
      }
    }

    // Update the trust store.
    if (exists) {
      List<Certificate> newCerts = getX509Certificate(newCertPath);
      trustCerts.addAll(newCerts);
      saveTo(trustStorePath, trustCerts);
      log.info("Truststore '{}' updated with new cert at alias '{}'", trustStorePath, certAlias);
    }

    // Backup up YBA's PEM trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));
  }

  public static void saveTo(String trustStorePath, List<Certificate> trustCerts)
      throws IOException {
    try (FileWriter fWriter = new FileWriter(trustStorePath);
        JcaPEMWriter pemWriter = new JcaPEMWriter(fWriter)) {
      int countCerts = 0;
      for (Certificate certificate : trustCerts) {
        pemWriter.writeObject(certificate);
        pemWriter.flush();
        countCerts += 1;
      }
      log.debug("Saved {} certificates to {}", countCerts, trustStorePath);
    }
  }

  public void remove(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws CertificateException, IOException, KeyStoreException {

    log.info("Removing cert {} from PEM truststore ...", certAlias);
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    List<Certificate> trustCerts = getCertificates(trustStorePath);
    // Check if such an alias already exists.
    List<Certificate> certToRemove = getX509Certificate(certPath);
    int certToRemoveCount = certToRemove.size();
    // Iterate through each certificate in certToRemove and check if it's used in any chain
    boolean exists = false;
    Iterator<Certificate> certIterator = certToRemove.iterator();
    while (certIterator.hasNext()) {
      Certificate cert = certIterator.next();
      if (isCertificateUsedInOtherChain(cert)) {
        // Certificate is part of a chain, do not remove it
        log.debug("Certificate {} is part of a chain, skipping removal.", certAlias);
        certToRemoveCount -= 1;
        certIterator.remove();
      } else {
        // Certificate is not part of a chain
        exists = true;
      }
    }

    if (certToRemoveCount == 0) {
      log.debug(
          "Skipping removal of cert from PEM truststore, as the cert is part of other trust chain");
      return;
    }

    if (!exists && !suppressErrors) {
      String msg = String.format("Certificate '%s' does not exist to delete", certAlias);
      log.error(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }

    // Delete from the trust-store.
    if (!certToRemove.isEmpty()) {
      Iterators.removeAll(trustCerts.iterator(), certToRemove);
      saveTo(trustStorePath, trustCerts);
      log.debug("Certificate {} is now deleted from trust-store {}", certAlias, trustStorePath);
    }
    log.info("custom CA certs deleted from YBA's PEM truststore");
  }

  public List<Certificate> getCertsInTrustStore(String trustStorePath, char[] trustStorePassword) {
    if (trustStorePath == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Cannot get CA certificates from empty path");
    }

    if (new File(trustStorePath).exists()) {
      log.debug("YBA's truststore already exists, returning certs from it");
    }
    List<Certificate> trustCerts = getCertificates(trustStorePath);
    if (trustCerts.isEmpty()) log.info("Initiating empty YBA trust-store");

    return trustCerts;
  }

  private List<Certificate> getCertificates(String trustStorePath) {
    List<Certificate> certs = new ArrayList<>();
    try (FileInputStream certStream = new FileInputStream(trustStorePath)) {
      CertificateFactory certificateFactory = CertificateFactory.getInstance("X.509");
      Certificate certificate = null;
      while ((certificate = certificateFactory.generateCertificate(certStream)) != null) {
        certs.add(certificate);
      }
    } catch (IOException | CertificateException e) {
      String msg = String.format("Extracted %d certs from %s.", certs.size(), trustStorePath);
      if (certs.isEmpty()) {
        log.warn(msg); // We expect certs to exist if a trust-store path exists.
      } else if (e.getMessage() != null && e.getMessage().trim().contains("Empty input")) {
        log.info(msg); // Reached EOF, should be ignored.
      } else {
        log.error(msg, e);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
      }
    }
    return certs;
  }

  public String getYbaTrustStorePath(String trustStoreHome) {
    // Get the existing trust bundle.
    return getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
  }

  public String getYbaTrustStoreType() {
    return "PEM";
  }

  public boolean isTrustStoreEmpty(String storePathStr, char[] trustStorePassword) {
    Path storePath = Paths.get(storePathStr);
    byte[] caBundleContent = new byte[0];
    try {
      caBundleContent = Files.readAllBytes(storePath);
      if (caBundleContent.length == 0) return true;
      else return false;
    } catch (IOException e) {
      String msg = String.format("Failed to read custom trust-store %s", storePathStr);
      log.error(msg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
  }

  private boolean isCertificateUsedInOtherChain(Certificate cert)
      throws CertificateException, IOException {
    // Retrieve all the certificates from `custom_ca_certificate_info` schema.
    // In case the passed cert is substring in more than 1 cert than it is used
    // as part of other cert chain as well.
    // We will skip removing it from the trust-store.
    int certChainCount = 0;
    List<CustomCaCertificateInfo> customCACertificates = CustomCaCertificateInfo.getAll(false);
    for (CustomCaCertificateInfo customCA : customCACertificates) {
      List<Certificate> certChain = getX509Certificate(customCA.getContents());
      if (certChain.contains(cert)) {
        certChainCount++;
      }
    }
    return certChainCount > 1;
  }
}

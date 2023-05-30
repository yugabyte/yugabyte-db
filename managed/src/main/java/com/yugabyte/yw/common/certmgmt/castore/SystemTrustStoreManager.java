// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.Iterators;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;

/** Relates to the Operating system's trust store */
@Slf4j
@Singleton
public class SystemTrustStoreManager implements TrustStoreManager {
  public static final String TRUSTSTORE_FILE_NAME = "yb-ca-bundle.pem";

  public boolean addCertificate(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    log.debug("Trying to update YBA's system truststore ...");

    Certificate newCert = null;
    newCert = getX509Certificate(certPath);
    if (newCert == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("No new CA certificate exists at %s", certPath));
    }

    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    boolean doesTrustStoreExist = new File(trustStorePath).exists();
    if (!doesTrustStoreExist) {
      File sysTrustStoreFile = new File(trustStorePath);
      sysTrustStoreFile.createNewFile();
      log.debug("Create an empty YBA system trust-store");
    } else {
      List<Certificate> trustCerts = getCertsInTrustStore(trustStorePath, trustStorePassword);
      if (trustCerts != null) {
        // Check if such an alias already exists.
        boolean exists = trustCerts.contains(newCert);
        if (exists && !suppressErrors) {
          String msg = "CA certificate with same content already exists";
          log.error(msg);
          throw new PlatformServiceException(BAD_REQUEST, msg);
        }
      }
    }

    // Update the trust store in file system.
    boolean append = true;
    CertificateHelper.writeCertFileContentToCertPath(
        (X509Certificate) newCert, trustStorePath, false, append);
    log.debug("Truststore '{}' now has the cert {}", trustStorePath, certAlias);

    // Backup up YBA's system trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));

    log.info("Custom CA certificate {} added in System truststore", certAlias);
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
    log.debug("Trying to replace cert {} in the System truststore {}..", certAlias, trustStorePath);
    List<Certificate> trustCerts = getCertificates(trustStorePath, trustStorePassword);

    // Check if such a cert already exists.
    Certificate oldCert = getX509Certificate(oldCertPath);
    boolean exists = trustCerts.remove(oldCert);
    if (!exists && !suppressErrors) {
      String msg = String.format("Certificate '%s' doesn't exist to update", certAlias);
      log.error(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }

    // Update the trust store.
    if (exists) {
      Certificate newCert = getX509Certificate(newCertPath);
      trustCerts.add(newCert);
      saveTo(trustStorePath, trustCerts);
      log.info("Truststore '{}' updated with new cert at alias '{}'", trustStorePath, certAlias);
    }

    // Backup up YBA's System trust store in DB.
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

    log.info("Removing cert {} from System truststore ...", certAlias);
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    List<Certificate> trustCerts = getCertificates(trustStorePath, trustStorePassword);

    // Check if such an alias already exists.
    Certificate certToRemove = getX509Certificate(certPath);
    boolean exists =
        Iterators.removeAll(trustCerts.iterator(), Collections.singletonList(certToRemove));

    if (!exists && !suppressErrors) {
      String msg = String.format("Certificate '%s' does not exist to delete", certAlias);
      log.error(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }

    // Delete from the trust-store.
    if (exists) {
      saveTo(trustStorePath, trustCerts);
      log.debug("Certificate {} is now deleted from trust-store {}", certAlias, trustStorePath);
    }
    log.info("custom CA certs deleted from YBA's System truststore");
  }

  private List<Certificate> getCertsInTrustStore(String trustStorePath, char[] trustStorePassword) {
    if (trustStorePath == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Cannot get CA certificates from empty path");
    }

    if (new File(trustStorePath).exists()) {
      log.debug("YBA's truststore already exists, returning certs from it");
    }
    List<Certificate> trustCerts = getCertificates(trustStorePath, null);
    if (trustCerts.isEmpty()) log.info("Initiating empty YBA trust-store");

    return trustCerts;
  }

  private List<Certificate> getCertificates(String trustStorePath, char[] trustStorePassword) {
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
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.datastax.driver.core.JdkSSLOptions;
import com.datastax.driver.core.SSLOptions;
import java.io.FileInputStream;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SslHelper {

  public static final Logger LOG = LoggerFactory.getLogger(SslHelper.class);

  public static SSLOptions getSSLOptions(String certfile) {
    try {
      CertificateFactory cf = CertificateFactory.getInstance("X.509");
      FileInputStream fis = new FileInputStream(certfile);
      X509Certificate ca;
      try {
        ca = (X509Certificate) cf.generateCertificate(fis);
      } catch (Exception e) {
        LOG.error("Exception generating certificate from input file: ", e);
        return null;
      } finally {
        fis.close();
      }

      // Create a KeyStore containing our trusted CAs.
      String keyStoreType = KeyStore.getDefaultType();
      KeyStore keyStore = KeyStore.getInstance(keyStoreType);
      keyStore.load(null, null);
      keyStore.setCertificateEntry("ca", ca);

      // Create a TrustManager that trusts the CAs in our KeyStore.
      String tmfAlgorithm = TrustManagerFactory.getDefaultAlgorithm();
      TrustManagerFactory tmf = TrustManagerFactory.getInstance(tmfAlgorithm);
      tmf.init(keyStore);

      SSLContext sslContext = SSLContext.getInstance("TLS");
      sslContext.init(null, tmf.getTrustManagers(), null);
      return JdkSSLOptions.builder().withSSLContext(sslContext).build();
    } catch (Exception e) {
      LOG.error("Exception creating sslContext: ", e);
      return null;
    }
  }
}

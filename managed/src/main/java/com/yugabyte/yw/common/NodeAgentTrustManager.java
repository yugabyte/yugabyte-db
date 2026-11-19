// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import java.io.IOException;
import java.net.Socket;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.Collection;
import javax.naming.InvalidNameException;
import javax.naming.ldap.LdapName;
import javax.naming.ldap.Rdn;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509ExtendedTrustManager;
import lombok.extern.slf4j.Slf4j;

/**
 * Trust manager for the node agent gRPC client. The JDK hostname check (RFC 2818) ignores the CN,
 * so a server cert that carries the node IP or hostname only in the CN is rejected. This falls back
 * to the CN when the standard SAN check fails, matching SecureRefiner::MatchEndpoint in
 * src/yb/rpc/secure_stream.cc: only the most specific CN is used, it may contain wildcards, and the
 * fallback applies even when the cert has SANs that do not match.
 */
@Slf4j
public class NodeAgentTrustManager extends X509ExtendedTrustManager {
  private final X509ExtendedTrustManager delegate;

  public NodeAgentTrustManager(Collection<X509Certificate> caCerts)
      throws GeneralSecurityException {
    // An empty in-memory store. It cannot be populated until loaded, even with a null stream.
    KeyStore keyStore = KeyStore.getInstance(KeyStore.getDefaultType());
    try {
      keyStore.load(null /* stream */, null /* password */);
    } catch (IOException e) {
      throw new GeneralSecurityException("Failed to initialize trust store", e);
    }
    int idx = 0;
    for (X509Certificate caCert : caCerts) {
      keyStore.setCertificateEntry("ca-" + idx++, caCert);
    }
    TrustManagerFactory factory =
        TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
    factory.init(keyStore);
    X509ExtendedTrustManager found = null;
    for (TrustManager trustManager : factory.getTrustManagers()) {
      if (trustManager instanceof X509ExtendedTrustManager) {
        found = (X509ExtendedTrustManager) trustManager;
        break;
      }
    }
    if (found == null) {
      throw new GeneralSecurityException("No X509ExtendedTrustManager found");
    }
    this.delegate = found;
  }

  @Override
  public void checkServerTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
      throws CertificateException {
    try {
      delegate.checkServerTrusted(chain, authType, engine);
    } catch (CertificateException e) {
      // Fallback to CN check if the standard SAN check fails.
      String peerHost = engine == null ? null : engine.getPeerHost();
      if (!matchesCommonName(chain, peerHost)) {
        throw e;
      }
      // The two-arg variant validates the chain without the endpoint identification check, so a
      // chain failure is still reported.
      delegate.checkServerTrusted(chain, authType);
      log.debug("Accepted node agent server cert for {} by CN", peerHost);
    }
  }

  @Override
  public void checkServerTrusted(X509Certificate[] chain, String authType, Socket socket)
      throws CertificateException {
    delegate.checkServerTrusted(chain, authType, socket);
  }

  @Override
  public void checkServerTrusted(X509Certificate[] chain, String authType)
      throws CertificateException {
    delegate.checkServerTrusted(chain, authType);
  }

  @Override
  public void checkClientTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
      throws CertificateException {
    delegate.checkClientTrusted(chain, authType, engine);
  }

  @Override
  public void checkClientTrusted(X509Certificate[] chain, String authType, Socket socket)
      throws CertificateException {
    delegate.checkClientTrusted(chain, authType, socket);
  }

  @Override
  public void checkClientTrusted(X509Certificate[] chain, String authType)
      throws CertificateException {
    delegate.checkClientTrusted(chain, authType);
  }

  @Override
  public X509Certificate[] getAcceptedIssuers() {
    return delegate.getAcceptedIssuers();
  }

  static boolean matchesCommonName(X509Certificate[] chain, String peerHost) {
    if (chain == null || chain.length == 0 || peerHost == null || peerHost.isEmpty()) {
      return false;
    }
    String commonName = null;
    try {
      // getRdns() is ordered least to most specific, so the last CN is the most specific one.
      for (Rdn rdn : new LdapName(chain[0].getSubjectX500Principal().getName()).getRdns()) {
        if ("CN".equalsIgnoreCase(rdn.getType())) {
          commonName = String.valueOf(rdn.getValue());
        }
      }
    } catch (InvalidNameException e) {
      log.warn("Failed to parse subject of node agent server cert", e);
    }
    return commonName != null && !commonName.isEmpty() && matchPattern(commonName, peerHost);
  }

  // Port of MatchPattern in src/yb/rpc/secure_stream.cc. A '*' matches any run of characters
  // within one DNS label. The comparison is case-insensitive.
  @VisibleForTesting
  static boolean matchPattern(String pattern, String host) {
    return matchPattern(pattern, 0, host, 0);
  }

  private static boolean matchPattern(String pattern, int p, String host, int h) {
    while (p < pattern.length() && h < host.length()) {
      char pc = pattern.charAt(p);
      if (pc == '*') {
        ++p;
        while (h < host.length() && host.charAt(h) != '.') {
          if (matchPattern(pattern, p, host, h)) {
            return true;
          }
          ++h;
        }
      } else if (Character.toLowerCase(pc) == Character.toLowerCase(host.charAt(h))) {
        ++p;
        ++h;
      } else {
        return false;
      }
    }
    return p == pattern.length() && h == host.length();
  }
}

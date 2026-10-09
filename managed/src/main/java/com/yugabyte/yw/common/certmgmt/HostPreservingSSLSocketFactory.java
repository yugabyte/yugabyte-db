// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.security.KeyManagementException;
import java.security.SecureRandom;
import javax.net.ssl.KeyManager;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLContextSpi;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLServerSocketFactory;
import javax.net.ssl.SSLSessionContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;

/**
 * SSLSocketFactory that refuses to create unconnected sockets, so that the TLS socket always learns
 * the host it is connecting to.
 *
 * <p>HttpsURLConnection prefers {@code createSocket()} followed by {@code connect(address)}, which
 * never passes the URL host to the TLS provider. SunJSSE is told the host through an internal API;
 * BCJSSE is not, and relied on the SNI the JDK used to set. Newer JDKs (e.g. Zulu 17.0.20) no
 * longer set SNI for IP literals, so BCJSSE fails endpoint identification for https://<ip> URLs
 * with "No hostname specified for HTTPS endpoint ID check".
 *
 * <p>When {@code createSocket()} throws a SocketException caused by UnsupportedOperationException
 * (the {@link javax.net.SocketFactory} default, which this class inherits), HttpsURLConnection
 * falls back to a plain socket layered with {@code createSocket(socket, host, port, autoClose)},
 * which hands BCJSSE the URL host. This is preferred over
 * org.bouncycastle.jsse.client.assumeOriginalHostName, which for IP literals verifies against the
 * reverse-DNS name instead of the IP.
 */
public class HostPreservingSSLSocketFactory extends SSLSocketFactory {

  private final SSLSocketFactory delegate;

  public HostPreservingSSLSocketFactory(SSLSocketFactory delegate) {
    this.delegate = delegate;
  }

  public static SSLSocketFactory wrap(SSLSocketFactory factory) {
    if (factory == null || factory instanceof HostPreservingSSLSocketFactory) {
      return factory;
    }
    return new HostPreservingSSLSocketFactory(factory);
  }

  /** For clients that only accept an SSLContext and call {@code getSocketFactory()} themselves. */
  public static SSLContext wrap(SSLContext context) {
    if (context == null) {
      return null;
    }
    return new SSLContext(
        new HostPreservingSSLContextSpi(context), context.getProvider(), context.getProtocol()) {};
  }

  @Override
  public String[] getDefaultCipherSuites() {
    return delegate.getDefaultCipherSuites();
  }

  @Override
  public String[] getSupportedCipherSuites() {
    return delegate.getSupportedCipherSuites();
  }

  @Override
  public Socket createSocket(Socket s, String host, int port, boolean autoClose)
      throws IOException {
    return delegate.createSocket(s, host, port, autoClose);
  }

  @Override
  public Socket createSocket(String host, int port) throws IOException {
    return delegate.createSocket(host, port);
  }

  @Override
  public Socket createSocket(String host, int port, InetAddress localHost, int localPort)
      throws IOException {
    return delegate.createSocket(host, port, localHost, localPort);
  }

  @Override
  public Socket createSocket(InetAddress host, int port) throws IOException {
    return delegate.createSocket(host, port);
  }

  @Override
  public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort)
      throws IOException {
    return delegate.createSocket(address, port, localAddress, localPort);
  }

  private static class HostPreservingSSLContextSpi extends SSLContextSpi {
    private final SSLContext delegate;

    HostPreservingSSLContextSpi(SSLContext delegate) {
      this.delegate = delegate;
    }

    @Override
    protected void engineInit(KeyManager[] km, TrustManager[] tm, SecureRandom sr)
        throws KeyManagementException {
      delegate.init(km, tm, sr);
    }

    @Override
    protected SSLSocketFactory engineGetSocketFactory() {
      return wrap(delegate.getSocketFactory());
    }

    @Override
    protected SSLServerSocketFactory engineGetServerSocketFactory() {
      return delegate.getServerSocketFactory();
    }

    @Override
    protected SSLEngine engineCreateSSLEngine() {
      return delegate.createSSLEngine();
    }

    @Override
    protected SSLEngine engineCreateSSLEngine(String host, int port) {
      return delegate.createSSLEngine(host, port);
    }

    @Override
    protected SSLSessionContext engineGetServerSessionContext() {
      return delegate.getServerSessionContext();
    }

    @Override
    protected SSLSessionContext engineGetClientSessionContext() {
      return delegate.getClientSessionContext();
    }

    // The SSLContextSpi defaults derive these from engineGetSocketFactory().createSocket(), which
    // this factory deliberately does not support.
    @Override
    protected SSLParameters engineGetDefaultSSLParameters() {
      return delegate.getDefaultSSLParameters();
    }

    @Override
    protected SSLParameters engineGetSupportedSSLParameters() {
      return delegate.getSupportedSSLParameters();
    }
  }
}

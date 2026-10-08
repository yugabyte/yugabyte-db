// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.net.SocketException;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
import org.junit.Before;
import org.junit.Test;

public class HostPreservingSSLSocketFactoryTest {

  private SSLContext context;

  @Before
  public void setUp() throws Exception {
    context = SSLContext.getInstance("TLS");
    context.init(null, null, null);
  }

  // HttpsURLConnection only falls back to createSocket(socket, host, port, autoClose) on exactly
  // this exception shape (see sun.net.www.protocol.https.HttpsClient#createSocket).
  @Test
  public void testUnconnectedSocketUnsupported() {
    SSLSocketFactory factory = HostPreservingSSLSocketFactory.wrap(context.getSocketFactory());
    SocketException e = assertThrows(SocketException.class, factory::createSocket);
    assertTrue(e.getCause() instanceof UnsupportedOperationException);
  }

  @Test
  public void testWrapFactory() {
    SSLSocketFactory delegate = context.getSocketFactory();
    SSLSocketFactory factory = HostPreservingSSLSocketFactory.wrap(delegate);
    assertTrue(factory instanceof HostPreservingSSLSocketFactory);
    assertSame(factory, HostPreservingSSLSocketFactory.wrap(factory));
    assertNull(HostPreservingSSLSocketFactory.wrap((SSLSocketFactory) null));
    assertArrayEquals(delegate.getDefaultCipherSuites(), factory.getDefaultCipherSuites());
    assertArrayEquals(delegate.getSupportedCipherSuites(), factory.getSupportedCipherSuites());
  }

  @Test
  public void testWrapContext() {
    SSLContext wrapped = HostPreservingSSLSocketFactory.wrap(context);
    assertTrue(wrapped.getSocketFactory() instanceof HostPreservingSSLSocketFactory);
    assertSame(context.getProvider(), wrapped.getProvider());
    // The SSLContextSpi defaults would call the unsupported createSocket().
    assertNotNull(wrapped.getDefaultSSLParameters());
    assertNotNull(wrapped.getSupportedSSLParameters());
    assertNotNull(wrapped.createSSLEngine("127.0.0.1", 443));
    assertNull(HostPreservingSSLSocketFactory.wrap((SSLContext) null));
  }
}

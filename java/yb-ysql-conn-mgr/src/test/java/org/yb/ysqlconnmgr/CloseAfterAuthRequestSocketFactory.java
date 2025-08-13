// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.ByteBuffer;

import javax.net.SocketFactory;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A SocketFactory implementation that creates sockets that intercept PostgreSQL protocol messages
 * and close the connection when a password authentication request is detected.
 *
 * This is useful for testing scenarios where you want to simulate connection failures during the
 * authentication phase.
 */
public class CloseAfterAuthRequestSocketFactory extends SocketFactory {
  private static final Logger LOG =
      LoggerFactory.getLogger(CloseAfterAuthRequestSocketFactory.class);

  // PostgreSQL message types
  private static final byte AUTHENTICATION_REQUEST = 'R';
  private static final int PASSWORD_AUTHENTICATION_METHOD = 3; // md5, 5 for plain text

  private final SocketFactory defaultFactory = SocketFactory.getDefault();

  public CloseAfterAuthRequestSocketFactory() {
    super();
  }

  @Override
  public Socket createSocket() throws IOException {
    return new InterceptingSocket();
  }

  @Override
  public Socket createSocket(String host, int port) throws IOException {
    return new InterceptingSocket(host, port);
  }

  @Override
  public Socket createSocket(String host, int port, InetAddress localHost, int localPort)
      throws IOException {
    return new InterceptingSocket(host, port, localHost, localPort);
  }

  @Override
  public Socket createSocket(InetAddress host, int port) throws IOException {
    return new InterceptingSocket(host, port);
  }

  @Override
  public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort)
      throws IOException {
    return new InterceptingSocket(address, port, localAddress, localPort);
  }

  /**
   * A socket that intercepts incoming data and closes the connection when a password authentication
   * request is detected.
   */
  private static class InterceptingSocket extends Socket {
    private InterceptingInputStream inputStream = null;

    public InterceptingSocket() throws IOException {
      super();
    }

    public InterceptingSocket(String host, int port) throws IOException {
      super(host, port);
    }

    public InterceptingSocket(String host, int port, InetAddress localHost, int localPort)
        throws IOException {
      super(host, port, localHost, localPort);
    }

    public InterceptingSocket(InetAddress host, int port) throws IOException {
      super(host, port);
    }

    public InterceptingSocket(InetAddress address, int port, InetAddress localAddress,
        int localPort) throws IOException {
      super(address, port, localAddress, localPort);
    }

    @Override
    public InputStream getInputStream() throws IOException {
      if (inputStream == null) {
        inputStream = new InterceptingInputStream(super.getInputStream());
      }
      return inputStream;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
      return super.getOutputStream();
    }
  }

  /**
   * An InputStream that intercepts data and checks for PostgreSQL authentication messages.
   */
  private static class InterceptingInputStream extends InputStream {
    private final InputStream delegate;
    private final ByteBuffer buffer = ByteBuffer.allocate(8192);
    private int bufferPosition = 0;
    private boolean closed = false;

    public InterceptingInputStream(InputStream delegate) {
      this.delegate = delegate;
    }

    @Override
    public int read() throws IOException {
      if (closed) {
        return -1;
      }

      int b = delegate.read();
      if (b != -1) {
        buffer.put(bufferPosition++, (byte) b);
        checkForPasswordRequest();
        if (closed) {
          return -1;
        }
      }
      return b;
    }

    @Override
    public int read(byte[] b) throws IOException {
      return read(b, 0, b.length);
    }

    @Override
    public int read(byte[] b, int off, int len) throws IOException {
      if (closed) {
        return -1;
      }

      int bytesRead = delegate.read(b, off, len);
      if (bytesRead > 0) {
        // Add the read bytes to our buffer for inspection
        for (int i = 0; i < bytesRead; i++) {
          buffer.put(bufferPosition++, b[off + i]);
        }
        checkForPasswordRequest();
        if (closed) {
          return -1;
        }
      }
      return bytesRead;
    }

    @Override
    public int available() throws IOException {
      return delegate.available();
    }

    @Override
    public void close() throws IOException {
      closed = true;
      delegate.close();
    }

    @Override
    public synchronized void mark(int readlimit) {
      delegate.mark(readlimit);
    }

    @Override
    public synchronized void reset() throws IOException {
      delegate.reset();
    }

    @Override
    public boolean markSupported() {
      return delegate.markSupported();
    }

    /**
     * Check if the buffered data contains a PostgreSQL password authentication request. PostgreSQL
     * protocol: 'R' (AuthenticationRequest) followed by 4-byte length, then 4-byte auth method.
     */
    private void checkForPasswordRequest() {
      if (bufferPosition < 1) {
        return; // Need at least 1 byte
      }

      // Look for authentication request messages in the buffer
      for (int i = 0; i < bufferPosition; i++) {
        if (buffer.get(i) == AUTHENTICATION_REQUEST) {
          LOG.info("Found authentication request at position " + i + ", closing socket");
          try {
            delegate.close();
          } catch (IOException e) {
            LOG.warn("Error closing socket after password request detection", e);
          }
          closed = true;
        }
      }

      // Keep only the last 8 bytes in case a message spans across reads
      // if (bufferPosition > 8) {
      // byte[] lastBytes = new byte[8];
      // buffer.position(bufferPosition - 8);
      // buffer.get(lastBytes);
      // buffer.clear();
      // buffer.put(lastBytes);
      // bufferPosition = 8;
      // }
    }
  }
}

// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.io.File;
import java.io.FileWriter;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.security.spec.ECGenParameterSpec;
import org.bouncycastle.util.io.pem.PemObject;
import org.bouncycastle.util.io.pem.PemWriter;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestAsyncYBClientPrivateKey {

  @Rule
  public TemporaryFolder tmpDir = new TemporaryFolder();

  @Test
  public void testRsaPkcs8Key() throws Exception {
    KeyPairGenerator gen = KeyPairGenerator.getInstance("RSA");
    gen.initialize(2048);
    assertKeyLoads(gen.generateKeyPair(), "RSA");
  }

  @Test
  public void testEcPkcs8Key() throws Exception {
    KeyPairGenerator gen = KeyPairGenerator.getInstance("EC");
    gen.initialize(new ECGenParameterSpec("secp256r1"));
    assertKeyLoads(gen.generateKeyPair(), "EC");
  }

  @Test
  public void testNonPkcs8KeyRejected() throws Exception {
    File keyFile = writePem("PRIVATE KEY", new byte[] {1, 2, 3, 4});
    try {
      AsyncYBClient.getPrivateKey(keyFile.getPath());
      fail("Expected loading a malformed key to fail");
    } catch (RuntimeException e) {
      assertTrue(e.getMessage(), e.getMessage().contains("Not a PKCS#8 private key"));
    }
  }

  private void assertKeyLoads(KeyPair keyPair, String expectedAlgorithm) throws Exception {
    // getEncoded() on a JDK private key returns PKCS#8.
    File keyFile = writePem("PRIVATE KEY", keyPair.getPrivate().getEncoded());
    PrivateKey loaded = AsyncYBClient.getPrivateKey(keyFile.getPath());
    assertEquals(expectedAlgorithm, loaded.getAlgorithm());
    assertArrayEquals(keyPair.getPrivate().getEncoded(), loaded.getEncoded());
  }

  private File writePem(String type, byte[] content) throws Exception {
    File keyFile = tmpDir.newFile();
    try (PemWriter writer = new PemWriter(new FileWriter(keyFile))) {
      writer.writeObject(new PemObject(type, content));
    }
    return keyFile;
  }
}

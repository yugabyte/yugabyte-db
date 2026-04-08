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
package org.yb.cql;

import static org.yb.AssertionWrappers.assertNotNull;

import java.io.IOException;
import java.math.BigInteger;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.cert.X509Certificate;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x500.X500NameBuilder;
import org.bouncycastle.asn1.x500.style.BCStyle;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.KeyUsage;
import org.bouncycastle.cert.X509CertificateHolder;
import org.bouncycastle.cert.X509v3CertificateBuilder;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.util.Pair;

import com.datastax.driver.core.ProtocolOptions;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSHeader;
import com.nimbusds.jose.JWSSigner;
import com.nimbusds.jose.crypto.ECDSASigner;
import com.nimbusds.jose.crypto.RSASSASigner;
import com.nimbusds.jose.jwk.Curve;
import com.nimbusds.jose.jwk.ECKey;
import com.nimbusds.jose.jwk.JWK;
import com.nimbusds.jose.jwk.JWKSet;
import com.nimbusds.jose.jwk.KeyUse;
import com.nimbusds.jose.jwk.RSAKey;
import com.nimbusds.jose.jwk.gen.ECKeyGenerator;
import com.nimbusds.jose.jwk.gen.RSAKeyGenerator;
import com.nimbusds.jose.util.Base64;
import com.nimbusds.jwt.JWTClaimsSet;
import com.nimbusds.jwt.SignedJWT;

import okhttp3.mockwebserver.Dispatcher;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;

@RunWith(value = YBTestRunner.class)
public class TestJWTAuth extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestJWTAuth.class);

  private static final String RS256_KEYID = "rs256_keyid";
  private static final String PS256_KEYID = "ps256_keyid";
  private static final String ES256_KEYID = "es256_keyid";
  private static final String RS256_KEYID_WITH_X5C = "rs256_keyid_with_x5c";
  private static final String PS256_KEYID_WITH_X5C = "ps256_keyid_with_x5c";
  private static final String ES256_KEYID_WITH_X5C = "es256_keyid_with_x5c";

  // The test shouldn't take 24 hours, so these constants are ok.
  private static final Date ISSUED_AT_TIME = new Date(new Date().getTime() - 24 * 60 * 60 * 1000);
  private static final Date EXPIRATION_TIME = new Date(new Date().getTime() + 24 * 60 * 60 * 1000);

  private static final List<String> ALLOWED_ISSUERS = new ArrayList<String>() {
    {
      add("login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0");
      add("oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0");
      add("example.com");
    }
  };
  private static final List<String> ALLOWED_AUDIENCES = new ArrayList<String>() {
    {
      add("724274f6-2156-11ee-be56-0242ac120002");
      add("795c2b42-2156-11ee-be56-0242ac120002");
      add("8ba558f0-2156-11ee-be56-0242ac120002-795c2b42-2156-11ee-be56-0242ac120002");
    }
  };

  private static final List<JWSAlgorithm> SUPPORTED_RSA_ALGORITHMS =
      Arrays.asList(JWSAlgorithm.RS256, JWSAlgorithm.RS384, JWSAlgorithm.RS512, JWSAlgorithm.PS256,
          JWSAlgorithm.PS384, JWSAlgorithm.PS512);
  private static final List<JWSAlgorithm> SUPPORTED_EC_ALGORITHMS = Arrays.asList(
      JWSAlgorithm.ES256, JWSAlgorithm.ES256K, JWSAlgorithm.ES384, JWSAlgorithm.ES512);

  private static X509Certificate generateSelfSignedCertificate(String certLabel,
      String algorithmName, PublicKey publicKey, PrivateKey PrivateKey) throws Exception {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.MONTH, 1);
    Date certExpiry = cal.getTime();

    X500Name subject = new X500NameBuilder(BCStyle.INSTANCE)
                           .addRDN(BCStyle.CN, certLabel)
                           .addRDN(BCStyle.O, "test.example.com")
                           .build();
    BigInteger serial = BigInteger.valueOf(System.currentTimeMillis());
    X509v3CertificateBuilder certGen =
        new JcaX509v3CertificateBuilder(subject, serial, certStart, certExpiry, subject, publicKey);
    BasicConstraints basicConstraints = new BasicConstraints(1);
    KeyUsage keyUsage = new KeyUsage(KeyUsage.digitalSignature | KeyUsage.keyCertSign);

    certGen.addExtension(Extension.basicConstraints, true, basicConstraints.toASN1Primitive());
    certGen.addExtension(Extension.keyUsage, true, keyUsage.toASN1Primitive());
    ContentSigner signer = new JcaContentSignerBuilder(algorithmName).build(PrivateKey);
    X509CertificateHolder holder = certGen.build(signer);
    JcaX509CertificateConverter converter = new JcaX509CertificateConverter();
    converter.setProvider(new BouncyCastleProvider());
    return converter.getCertificate(holder);
  }

  private JWKSet jwks;

  // Create JWKS with keys for RSA and EC family of algorithms. Both of them are asymmetric key
  // based algorithms.
  private static JWKSet createJwks() throws Exception {
    RSAKey rs256Key = new RSAKeyGenerator(2048)
                          .keyID(RS256_KEYID)
                          .algorithm(JWSAlgorithm.RS256)
                          .generate();
    RSAKey ps256Key = new RSAKeyGenerator(2048)
                          .keyID(PS256_KEYID)
                          .algorithm(JWSAlgorithm.PS256)
                          .generate();
    ECKey ecKey = new ECKeyGenerator(Curve.P_256)
                      .keyID(ES256_KEYID)
                      .algorithm(JWSAlgorithm.ES256)
                      .generate();

    // Create JWKs with x5c and x5t parameters.

    X509Certificate rs256KeyX5cCert = generateSelfSignedCertificate(
        RS256_KEYID_WITH_X5C, "SHA256withRSA", rs256Key.toPublicKey(), rs256Key.toPrivateKey());
    RSAKey rs256KeyX5c =
        new RSAKey.Builder(rs256Key)
            .keyID(RS256_KEYID_WITH_X5C)
            .x509CertChain(Collections.singletonList(Base64.encode(rs256KeyX5cCert.getEncoded())))
            .x509CertThumbprint(rs256Key.computeThumbprint())
            .build();

    X509Certificate ps256KeyX5cCert = generateSelfSignedCertificate(
        PS256_KEYID_WITH_X5C, "SHA256withRSA", ps256Key.toPublicKey(), ps256Key.toPrivateKey());
    RSAKey ps256KeyX5c =
        new RSAKey.Builder(ps256Key)
            .keyID(PS256_KEYID_WITH_X5C)
            .x509CertChain(Collections.singletonList(Base64.encode(ps256KeyX5cCert.getEncoded())))
            .x509CertThumbprint(ps256Key.computeThumbprint())
            .build();

    X509Certificate ecKeyX5cCert = generateSelfSignedCertificate(
        ES256_KEYID_WITH_X5C, "SHA256withECDSA", ecKey.toPublicKey(), ecKey.toPrivateKey());
    ECKey ecKeyX5c =
        new ECKey.Builder(ecKey)
            .keyID(ES256_KEYID_WITH_X5C)
            .x509CertChain(Collections.singletonList(Base64.encode(ecKeyX5cCert.getEncoded())))
            .x509CertThumbprint(ecKey.computeThumbprint())
            .build();

    return new JWKSet(new ArrayList<JWK>() {
      {
        add(rs256Key);
        add(ps256Key);
        add(ecKey);
        add(rs256KeyX5c);
        add(ps256KeyX5c);
        add(ecKeyX5c);
      }
    });
  }

  // groupsOrRoles needs to be passed separately since Nimbus is not able to serialize the List when
  // it receives it as a object.
  private static String createJWT(JWSAlgorithm algorithm, JWKSet jwks, String keyId, String sub,
      String issuer, String audience, Date issuedAtTime, Date expirationTime,
      Map<String, String> optionalClaims, Pair<String, List<String>> groupsOrRoles)
      throws Exception {
    JWK key = jwks.getKeyByKeyId(keyId);
    assertNotNull(key);

    JWSSigner signer = null;
    if (SUPPORTED_RSA_ALGORITHMS.contains(algorithm)) {
      signer = new RSASSASigner(key.toRSAKey().toPrivateKey());
    } else if (SUPPORTED_EC_ALGORITHMS.contains(algorithm)) {
      signer = new ECDSASigner(key.toECKey().toECPrivateKey());
    }
    assertNotNull(signer);

    JWTClaimsSet.Builder claimsSetBuilder = new JWTClaimsSet.Builder()
                                                .subject(sub)
                                                .issuer(issuer)
                                                .audience(audience)
                                                .expirationTime(expirationTime)
                                                .issueTime(issuedAtTime);

    for (Map.Entry<String, String> entry : optionalClaims.entrySet()) {
      claimsSetBuilder.claim(entry.getKey(), entry.getValue());
    }

    // Set the groups/roles.
    if (groupsOrRoles != null) {
      claimsSetBuilder.claim(groupsOrRoles.getFirst(), groupsOrRoles.getSecond());
    }

    SignedJWT signedJWT = new SignedJWT(
        new JWSHeader.Builder(algorithm).keyID(key.getKeyID()).build(), claimsSetBuilder.build());

    signedJWT.sign(signer);
    return signedJWT.serialize();
  }

  private static String createJWT(JWSAlgorithm algorithm, JWKSet jwks, String keyId, String sub,
      String issuer, String audience, Date issuedAtTime, Date expirationTime,
      Pair<String, List<String>> groupsOrRoles) throws Exception {
    return createJWT(algorithm, jwks, keyId, sub, issuer, audience, issuedAtTime, expirationTime,
        new HashMap<String, String>(), groupsOrRoles);
  }

  private void setJWTConfigAndRestartCluster(List<String> allowedIssuers,
      List<String> allowedAudiences, String matchingClaimKey, String jwksUrl, String identConfCsv)
      throws Exception {
    String issuersCsv = String.join(",", allowedIssuers);
    String audiencesCsv = String.join(",", allowedAudiences);

    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ycql_use_jwt_auth", "true");
    flagMap.put("allowed_preview_flags_csv", "ycql_use_jwt_auth");
    flagMap.put("ycql_jwt_users_to_skip_csv", "cassandra");

    // Construct the combined options string
    StringBuilder jwtOptions = new StringBuilder();
    jwtOptions.append("jwt_issuers=").append(issuersCsv);
    jwtOptions.append(" jwt_audiences=").append(audiencesCsv);
    jwtOptions.append(" jwt_jwks_url=").append(jwksUrl);
    if (!matchingClaimKey.isEmpty()) {
      jwtOptions.append(" jwt_matching_claim_key=").append(matchingClaimKey);
    }
    flagMap.put("ycql_jwt_conf", jwtOptions.toString());
    if (!identConfCsv.isEmpty()) {
      flagMap.put("ycql_ident_conf_csv", identConfCsv);
    }
    flagMap.put("vmodule", "cql_processor=4,cql_service=4,jwt_util=4");
    restartClusterWithTSFlags(flagMap);
    LOG.info("Cluster restart finished");
  }

  private void setJWTConfigAndRestartCluster(List<String> allowedIssuers,
      List<String> allowedAudiences, String matchingClaimKey, String jwksUrl) throws Exception {
    setJWTConfigAndRestartCluster(
        allowedIssuers, allowedAudiences, matchingClaimKey, jwksUrl, "" /* identConfCsv */);
  }

  private String configureMockJwksServer(MockWebServer server) throws IOException {
    Dispatcher mDispatcher = new Dispatcher() {
        @Override
        public MockResponse dispatch(RecordedRequest request) {
          if (request.getPath().contains("/jwks_keys")) {
            return new MockResponse().setResponseCode(200)
                                     .setBody(jwks.toString(true));
          }
          if (request.getPath().contains("/invalid_json")) {
            return new MockResponse().setResponseCode(200)
                                      .setBody("invalid json");
          }
          return new MockResponse().setResponseCode(404);
        }
      };
      server.setDispatcher(mDispatcher);
      server.start();
      return String.format("http://%s:%s/jwks_keys",
                           server.getHostName(), server.getPort());
  }

  @Before
  public void setUp() throws Exception {
    jwks = createJwks();
  }

  @Test
  public void authWithSubject() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      String serverUrl = configureMockJwksServer(server);

      setJWTConfigAndRestartCluster(
          ALLOWED_ISSUERS, ALLOWED_AUDIENCES, /* matchingClaimKey */ "", serverUrl);

      List<Pair<JWSAlgorithm, String>> keysWithAlgorithms =
        new ArrayList<Pair<JWSAlgorithm, String>>() {
          {
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID_WITH_X5C));
          }
        };

      session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");
      session.execute("CREATE ROLE 'testUser2' WITH LOGIN = true");

      // Ensure that login works with each key type.
      for (Pair<JWSAlgorithm, String> key : keysWithAlgorithms) {
        String jwt = createJWT(key.getFirst(), jwks, key.getSecond(), "testUser1",
            "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
            "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
        checkConnectivity(
            true, "testUser1", jwt, ProtocolOptions.Compression.NONE, false /* expectFailure */);

        // Identity mismatch since IDP username: testUser1 while YCQL username: testUser2.
        checkConnectivityWithMessage(true, "testUser2", jwt, ProtocolOptions.Compression.NONE,
            true /* expectFailure */,
            "Failed to authenticate using JWT: Provided username 'testUser2' and/or token are "
            + "incorrect");
      }

      // Basic JWT login with invalid JWT.
      checkConnectivity(true, "testUser1", "invalid_jwt", ProtocolOptions.Compression.NONE,
          true /* expectFailure */);

      server.shutdown();
      session.execute("DROP ROLE 'testUser1'");
      session.execute("DROP ROLE 'testUser2'");
    }
  }

  // YCQL does not allow '@' in the username, so using email without ident mappings is not possible.
  // YCQL username: testUser1
  // IDP email: testUser1@example.com
  // Ident mapping: /^(.*)@example\\.com$      \1
  @Test
  public void authWithEmailWithIdent() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      String serverUrl = configureMockJwksServer(server);

      setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES,
          /* matchingClaimKey */ "email", serverUrl,
          /* identConfCsv */ "\"/^(.*)@example\\.com$ \\1\"");

      List<Pair<JWSAlgorithm, String>> keysWithAlgorithms =
        new ArrayList<Pair<JWSAlgorithm, String>>() {
          {
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID_WITH_X5C));
          }
        };

      session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");
      session.execute("CREATE ROLE 'testUser2' WITH LOGIN = true");

      // Ensure that login works with each key type.
      for (Pair<JWSAlgorithm, String> key : keysWithAlgorithms) {
        String jwt = createJWT(key.getFirst(), jwks, key.getSecond(), "AnySubject_Doesnotmatter",
            "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
            "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME,
            EXPIRATION_TIME, new HashMap<String, String>() {
              {
                put("email", "testUser1@example.com");
              }
            }, null);
        checkConnectivity(
            true, "testUser1", jwt, ProtocolOptions.Compression.NONE, false /* expectFailure */);

        // Identity mismatch since IDP username after applying regex mapping: testUser1 while YCQL
        // username: testUser2.
        checkConnectivityWithMessage(true, "testUser2", jwt, ProtocolOptions.Compression.NONE,
            true /* expectFailure */,
            "Failed to authenticate using JWT: Provided username 'testUser2' and/or token are "
                + "incorrect");
      }

      server.shutdown();
      session.execute("DROP ROLE 'testUser1'");
      session.execute("DROP ROLE 'testUser2'");
    }
  }

  @Test
  public void invalidAuthentication() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      String serverUrl = configureMockJwksServer(server);

      setJWTConfigAndRestartCluster(
          ALLOWED_ISSUERS, ALLOWED_AUDIENCES, /* matchingClaimKey */ "", serverUrl);

      session.execute("CREATE ROLE 'testuser1' WITH LOGIN = true");

      // Valid login just for sanity check.
      String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
          "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, false /* expectFailure */);

      // Invalid Issuer.
      jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
          "login.issuer1.secured.example.com/some_invalid_id/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);

      // Invalid Audience.
      jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
          "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
          "12344_incorrect_audience", ISSUED_AT_TIME, EXPIRATION_TIME, null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);

      // Null value of matching claim key.
      jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, null,
          "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);

      // Token already expired 10 minutes ago.
      jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
          "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME,
          new Date(new Date().getTime() - 10 * 60 * 1000), null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);

      // Token issued 10 minutes in the future.
      // Note: The JWT-CPP library used internally classifies this also as an expired token.
      jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
          "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", new Date(new Date().getTime() + 10 * 60 * 1000),
          EXPIRATION_TIME, null);
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);

      // Token signed by a different key than the ones present in JWKS.
      RSAKey rs256Key = new RSAKeyGenerator(2048)
                            .keyUse(KeyUse.SIGNATURE)
                            .keyID("not_present_in_jwks")
                            .algorithm(JWSAlgorithm.RS256)
                            .generate();
      JWSSigner signer = new RSASSASigner(rs256Key.toRSAKey().toPrivateKey());
      JWTClaimsSet.Builder claimsSetBuilder =
          new JWTClaimsSet.Builder()
              .subject("testuser1")
              .issuer(
                  "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0")
              .audience("795c2b42-2156-11ee-be56-0242ac120002")
              .expirationTime(EXPIRATION_TIME);
      SignedJWT signedJWT = new SignedJWT(
          new JWSHeader.Builder(JWSAlgorithm.RS256).keyID("not_present_in_jwks").build(),
          claimsSetBuilder.build());

      signedJWT.sign(signer);
      jwt = signedJWT.serialize();
      checkConnectivity(
          true, "testuser1", jwt, ProtocolOptions.Compression.NONE, true /* expectFailure */);
    }
  }

  @Test
  public void authWithGroupsOrRolesWithoutIdent() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      String serverUrl = configureMockJwksServer(server);

      setJWTConfigAndRestartCluster(
          ALLOWED_ISSUERS, ALLOWED_AUDIENCES, /* matchingClaimKey */ "groups", serverUrl);

      List<Pair<JWSAlgorithm, String>> keysWithAlgorithms =
        new ArrayList<Pair<JWSAlgorithm, String>>() {
          {
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID_WITH_X5C));
          }
        };

      session.execute("CREATE ROLE 'dbadmintest' WITH LOGIN = true");
      session.execute("CREATE ROLE 'dbadmintest2' WITH LOGIN = true");

      for (Pair<JWSAlgorithm, String> key : keysWithAlgorithms) {
        String jwt = createJWT(key.getFirst(), jwks, key.getSecond(), "AnySubject_Doesnotmatter",
            "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
            "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME,
            new HashMap<String, String>() {
              {
                put("email", "doesnotmatter@random.com");
              } // doesn't matter.
            },
            new Pair<String, List<String>>(
                "groups", Arrays.asList("anothergroup", "dbadmintest", "anotheranothergroup@xyz")));
        // Login must succeed as the user is part of the "dbadmintest" group.
        checkConnectivity(
            true, "dbadmintest", jwt, ProtocolOptions.Compression.NONE, false /* expectFailure */);

        // Identity mismatch since the user's group doesn't contain the YCQL username: dbadmintest2.
        checkConnectivityWithMessage(true, "dbadmintest2", jwt, ProtocolOptions.Compression.NONE,
            true /* expectFailure */,
            "Failed to authenticate using JWT: Provided username 'dbadmintest2' and/or token are "
            + "incorrect");
      }

      server.shutdown();
      session.execute("DROP ROLE 'dbadmintest'");
      session.execute("DROP ROLE 'dbadmintest2'");
    }
  }

  @Test
  public void authWithGroupsOrRolesWithIdent() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      String serverUrl = configureMockJwksServer(server);

      setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES,
          /* matchingClaimKey */ "groups", serverUrl,
          /* identConfCsv */ "\"/^(.*)@example\\.com$ \\1\"");

      List<Pair<JWSAlgorithm, String>> keysWithAlgorithms =
        new ArrayList<Pair<JWSAlgorithm, String>>() {
          {
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.RS256, RS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.PS256, PS256_KEYID_WITH_X5C));
            add(new Pair<JWSAlgorithm, String>(JWSAlgorithm.ES256, ES256_KEYID_WITH_X5C));
          }
        };

      session.execute("CREATE ROLE 'dbadmintest' WITH LOGIN = true");
      session.execute("CREATE ROLE 'dbadmintest2' WITH LOGIN = true");

      for (Pair<JWSAlgorithm, String> key : keysWithAlgorithms) {
        String jwt = createJWT(key.getFirst(), jwks, key.getSecond(), "AnySubject_Doesnotmatter",
            "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
            "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME,
            new HashMap<String, String>() {
              {
                put("email", "doesnotmatter@random.com");
              } // doesn't matter.
            },
            new Pair<String, List<String>>("groups",
                Arrays.asList(
                    "anothergroup", "dbadmintest@example.com", "anotheranothergroup@xyz")));
        // Login must succeed as the user is part of the "dbadmintest@example.com" group which after
        // applying the ident mapping is equal to the YCQL username: dbadmintest.
        checkConnectivity(
            true, "dbadmintest", jwt, ProtocolOptions.Compression.NONE, false /* expectFailure */);

        // Identity mismatch since the user's group doesn't contain the YCQL username: dbadmintest2
        // after applying the ident mapping.
        checkConnectivityWithMessage(true, "dbadmintest2", jwt, ProtocolOptions.Compression.NONE,
            true /* expectFailure */,
            "Failed to authenticate using JWT: Provided username 'dbadmintest2' and/or token are "
            + "incorrect");
      }

      server.shutdown();
      session.execute("DROP ROLE 'dbadmintest'");
      session.execute("DROP ROLE 'dbadmintest2'");
    }
  }
}

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
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.fail;

import java.io.File;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.charset.Charset;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.cert.X509Certificate;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
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
import org.yb.client.TestUtils;
import org.yb.util.Pair;

import com.google.common.base.Strings;
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
import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunner.class)
public class TestJWTAuth extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestJWTAuth.class);

  private static final String INCORRECT_JWT_AUTH_MSG = "JWT authentication failed for user";

  private static final String JWKS_FILE_NAME = "jwt_jwks";
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

  private static String populateJWKSFile(String content) throws IOException {
    String jwksPath = TestUtils.getBaseTmpDir() + "/" + JWKS_FILE_NAME;
    File f = new File(jwksPath);
    FileUtils.writeStringToFile(f, content, Charset.defaultCharset());

    String quotedJwksPath =  String.format("\"\"%s\"\"", jwksPath);
    LOG.info(String.format("The jwksPath = %s and the JWKS content = %s", quotedJwksPath, content));
    return quotedJwksPath;
  }

  private static String populateJWKSFile(JWKSet jwks) throws IOException {
    String content = jwks.toString(/* publicKeysOnly */ true);
    return populateJWKSFile(content);
  }

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
  private String jwksPath;

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

  // Sets up JWT authentication with the provided configuration params.
  // Enables JWT auth on "testuser1" while the remaining users authenticate via trust.
  private void setJWTConfigAndRestartCluster(List<String> allowedIssuers,
      List<String> allowedAudiences, String jwksPath, String matchingClaimKey, String mapName,
      String identFileContents) throws Exception {
    String issuersCsv = String.join(",", allowedIssuers);
    String audiencesCsv = String.join(",", allowedAudiences);

    String matchingClaimKeyValues = "";
    if (!Strings.isNullOrEmpty(matchingClaimKey)) {
      matchingClaimKeyValues = String.format("jwt_matching_claim_key=%s", matchingClaimKey);
    }

    Map<String, String> flagMap = super.getTServerFlags();
    String hba_conf_value = "";
    if (Strings.isNullOrEmpty(mapName)) {
      hba_conf_value = String.format("\"host all yugabyte 0.0.0.0/0 trust\","
              + "\"host all yugabyte_test 0.0.0.0/0 trust\","
              + "\"host all all 0.0.0.0/0 jwt "
              + "jwt_jwks_path=%s "
              + "jwt_issuers=\"\"%s\"\" "
              + "jwt_audiences=\"\"%s\"\" %s\"",
          jwksPath, issuersCsv, audiencesCsv, matchingClaimKeyValues);
    } else {
      hba_conf_value = String.format("\"host all yugabyte 0.0.0.0/0 trust\","
              + "\"host all yugabyte_test 0.0.0.0/0 trust\","
              + "\"host all all 0.0.0.0/0 jwt "
              + "jwt_jwks_path=%s "
              + "jwt_issuers=\"\"%s\"\" "
              + "jwt_audiences=\"\"%s\"\" %s map=%s\"",
          jwksPath, issuersCsv, audiencesCsv, matchingClaimKeyValues, mapName);
    }

    flagMap.put("ysql_hba_conf_csv", hba_conf_value);
    LOG.info("ysql_hba_conf_csv = " + flagMap.get("ysql_hba_conf_csv"));

    if (!Strings.isNullOrEmpty(mapName)) {
      flagMap.put("ysql_ident_conf_csv", identFileContents);
      LOG.info("ysql_ident_conf_csv = " + flagMap.get("ysql_ident_conf_csv"));
    }

    restartClusterWithFlags(Collections.emptyMap(), flagMap);
    LOG.info("Cluster restart finished");
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

  private static String createJWT(JWSAlgorithm algorithm, JWKSet jwks, String keyId, String sub,
      String issuer, String audience, Date issuedAtTime, Date expirationTime) throws Exception {
    return createJWT(algorithm, jwks, keyId, sub, issuer, audience, issuedAtTime, expirationTime,
        new HashMap<String, String>(), null);
  }

  @Before
  public void setUp() throws Exception {
    jwks = createJwks();
    jwksPath = populateJWKSFile(jwks);
  }

  void assertSuccessfulAuthentication(ConnectionBuilder passRoleUserConnBldr, String jwt)
      throws Exception {
    try (Connection connection = passRoleUserConnBldr.withPassword(jwt).connect()) {
      // No-op.
    }
  }

  @Test
  public void authWithSubjectWithoutIdent() throws Exception {
    // "sub" is used as the default matching claim key.
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "", /* identFileContents */ "");
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

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Ensure that login works with each key type.
    for (Pair<JWSAlgorithm, String> key : keysWithAlgorithms) {
      String jwt = createJWT(key.getFirst(), jwks, key.getSecond(), "testuser1",
          "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
          "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
      assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);
    }

    // Basic JWT login with incorrect password.
    assertFailedAuthentication(passRoleUserConnBldr, "123");
  }

  @Test
  public void authWithSubjectWithIdent() throws Exception {
    // Map IDP {name}@example.com to YSQL {name}.
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "map1",
        /* identFileContents */ "\"map1   /^(.*)@example\\.com$      \\1\"");

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Subject is testuser1@example.com and the YSQL user is testuser1. The mapping should allow
    // login.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1@example.com",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);

    // Subject is testuser1@random.com, so it should fail.
    String jwtWithDifferentSubject = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID,
        "testuser1@random.com",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwtWithDifferentSubject);
  }

  // YSQL does not allow '@' in the username, so using email without ident mappings is not possible.
  @Test
  public void authWithEmailWithIdent() throws Exception {
    // Map IDP {name}@example.com to YSQL {name}.
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "email", /* mapName */ "map1",
        /* identFileContents */ "\"map1   /^(.*)@example\\.com$      \\1\"");

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Email is testuser1@example.com and the YSQL user is testuser1. The mapping should allow
    // login.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "AnySubject_Doesnotmatter",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME,
        EXPIRATION_TIME, new HashMap<String, String>() {
          { put("email", "testuser1@example.com"); }
        }, null);
    assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);

    // Email is testuser1@random.com, so it should fail.
    String jwtWithDifferentEmail =
        createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "AnySubject_Doesnotmatter",
            "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
            "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME,
            EXPIRATION_TIME, new HashMap<String, String>() {
              { put("email", "testuser1@random.com"); }
            }, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwtWithDifferentEmail);
  }

  @Test
  public void authWithGroupsOrRolesWithoutIdent() throws Exception {
    // roles is exactly the same as well.
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "groups", /* mapName */ "", /* identFileContents */ "");

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE dbadmintest LOGIN"); // YSQL user representing a group.
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("dbadmintest");

    // The user is part of the dbadmintest group, so login must be successful.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "Anysubject_Doesnotmatter",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME,
        new HashMap<String, String>() {
          { put("email", "doesnotmatter@random.com"); } // doesn't matter.
        },
        new Pair<String, List<String>>(
            "groups", Arrays.asList("anothergroup", "dbadmintest", "anotheranothergroup@xyz")));
    assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);

    // The user is not the part of the dbadmintest group, so login must fail.
    String jwtWithDifferentGroups = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID,
        "AnySubject_Doesnotmatter",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME,
        new HashMap<String, String>() {
          { put("email", "doesnotmatter@random.com"); } // doesn't matter.
        },
        new Pair<String, List<String>>("groups",
            Arrays.asList("dbadmintest-extrasuffix", "anothergroup", "anotheranothergroup@xyz")));
    assertFailedAuthentication(passRoleUserConnBldr, jwtWithDifferentGroups);
  }

  @Test
  public void authWithGroupsOrRolesWithIdent() throws Exception {
    // Map IDP {name}@example.com to YSQL {name}.
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "groups", /* mapName */ "map1",
        /* identFileContents */ "\"map1   /^(.*)@example\\.com$      \\1\"");

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE dbadmintest LOGIN"); // YSQL user representing a group.
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("dbadmintest");

    // The user is part of the dbadmintest@example.com group which gets mapped to dbadmintest, so
    // login must be successful.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "Anysubject_Doesnotmatter",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002",  ISSUED_AT_TIME, EXPIRATION_TIME,
        new HashMap<String, String>() {
          { put("email", "doesnotmatter@random.com"); } // doesn't matter.
        },
        new Pair<String, List<String>>("groups",
            Arrays.asList("anothergroup", "dbadmintest@example.com", "anotheranothergroup@xyz")));
    assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);

    // The user is not the part of the dbadmintest@example.com group, so login must fail.
    String jwtWithDifferentGroups = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID,
        "AnySubject_Doesnotmatter",
        "login.issuer1.secured.example.com/2ac843f8-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002",  ISSUED_AT_TIME, EXPIRATION_TIME,
        new HashMap<String, String>() {
          { put("email", "doesnotmatter@random.com"); } // doesn't matter.
        },
        new Pair<String, List<String>>("groups",
            Arrays.asList(
                "dbadmintest", // dbadmintest doesn't match regex pattern dbadmintest@example.com
                "dbadmintest-extrasuffix@example.com", "dbadmintest@anotherexample.com",
                "anotheranothergroup@xyz")));
    assertFailedAuthentication(passRoleUserConnBldr, jwtWithDifferentGroups);
  }

  @Test
  public void identMapping() throws Exception {
    String identFileContents = "\"map1  93fba67c-6a2a-40ae-9984-43fbe0a83d08  somename\","
        + "\"map1  john  johndoe\"," // IDP user john is johndoe in YSQL.
        + "\"map1  /^(.*)@example\\.com$  \\1\"," // convert {name}@example.com to {name}
        + "\"map1  /abcd97e5-*  prefixuser\""; // Any IDP username with prefix 'abcd97e5-' maps to
                                               // prefixuser.

    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "map1", identFileContents);

    // Create some users.
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE somename LOGIN");
      statement.execute("CREATE ROLE johndoe LOGIN");
      statement.execute("CREATE ROLE prefixuser LOGIN");
    }

    // (Subject, YSQL username).
    List<Pair<String, String>> testCases =
        Arrays.asList(new Pair<String, String>("john", "johndoe"),
            new Pair<String, String>("93fba67c-6a2a-40ae-9984-43fbe0a83d08", "somename"),
            new Pair<String, String>("johndoe@example.com", "johndoe"),
            new Pair<String, String>("abcd97e5-a", "prefixuser"),
            new Pair<String, String>("abcd97e5-b", "prefixuser"));

    for (Pair<String, String> testCase : testCases) {
      String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, testCase.getFirst(),
          ALLOWED_ISSUERS.get(0), ALLOWED_AUDIENCES.get(0), ISSUED_AT_TIME, EXPIRATION_TIME);
      assertSuccessfulAuthentication(getConnectionBuilder().withUser(testCase.getSecond()), jwt);
    }
  }

  private static void assertFailedAuthentication(
      ConnectionBuilder passRoleUserConnBldr, String password) throws Exception {
    try (Connection connection = passRoleUserConnBldr.withPassword(password).connect()) {
      fail("Expected exception but did not get any.");
    } catch (PSQLException e) {
      if (StringUtils.containsIgnoreCase(e.getMessage(), INCORRECT_JWT_AUTH_MSG)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
            e.getMessage(), INCORRECT_JWT_AUTH_MSG));
      }
    }
  }

  @Test
  public void illformattedRegexMapping() throws Exception {
    // We didn't specify who the IDP user "john" maps to.
    String identFileContents = "\"map1 john \"";

    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "map1", identFileContents);

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE johndoe LOGIN");
    }

    // Create and use JWT for user "john". Illformatted mapping should lead to failed authentication
    // but not a crash.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "john", ALLOWED_ISSUERS.get(0),
        ALLOWED_AUDIENCES.get(0), ISSUED_AT_TIME, EXPIRATION_TIME);
    assertFailedAuthentication(getConnectionBuilder().withUser("johndoe"), jwt);
  }

  // TODO: Some of these test cases can be moved to C++ unit tests for jwt_util.cc which are much
  // faster.
  @Test
  public void invalidAuthentication() throws Exception {
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "", /* identFileContents */ "");

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Valid login just for sanity check.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertSuccessfulAuthentication(passRoleUserConnBldr, jwt);

    // Invalid Issuer.
    jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "login.issuer1.secured.example.com/some_invalid_id/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);

    // Invalid Audience.
    jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
        "12344_incorrect_audience", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);

    // Null value of matching claim key.
    jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, null,
        "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);

    // Token already expired 10 minutes ago.
    jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME,
        new Date(new Date().getTime() - 10 * 60 * 1000), null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);

    // Token issued 10 minutes in the future.
    // Note: The JWT-CPP library used internally classifies this also as an expired token.
    jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", new Date(new Date().getTime() + 10 * 60 * 1000),
        EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);

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
            .issuer("oidc.issuer2.unsecured.example.com/4ffa94aa-2156-11ee-be56-0242ac120002/v2.0")
            .audience("795c2b42-2156-11ee-be56-0242ac120002")
            .expirationTime(EXPIRATION_TIME);
    SignedJWT signedJWT = new SignedJWT(
        new JWSHeader.Builder(JWSAlgorithm.RS256).keyID("not_present_in_jwks").build(),
        claimsSetBuilder.build());

    signedJWT.sign(signer);
    jwt = signedJWT.serialize();
    assertFailedAuthentication(passRoleUserConnBldr, jwt);
  }

  void testFailedAuthentication(JWKSet jwks) throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // JWKS json is parsed and validated during authentication.
    String jwt = createJWT(JWSAlgorithm.RS256, jwks, RS256_KEYID, "testuser1",
        "login.issuer1.secured.example.com/some_invalid_id/v2.0",
        "795c2b42-2156-11ee-be56-0242ac120002", ISSUED_AT_TIME, EXPIRATION_TIME, null);
    assertFailedAuthentication(passRoleUserConnBldr, jwt);
  }

  @Test
  public void invalidJWTJwksPath() throws Exception {
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, "/some/invalid/path",
        /* matchingClaimKey */ "", /* mapName */ "", /* identFileContents */ "");

    testFailedAuthentication(jwks);
  }

  @Test
  public void invalidJWKSJson() throws Exception {
    String jwksPath = populateJWKSFile("some_invalid_json");
    setJWTConfigAndRestartCluster(ALLOWED_ISSUERS, ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "", /* mapName */ "", /* identFileContents */ "");

    testFailedAuthentication(jwks);
  }

  // Asserts that the cluster restart failed by expecting an exception. There doesn't seem to be a
  // more accurate way of checking that.
  private void assertClusterRestartFailure(List<String> allowedIssuers,
      List<String> allowedAudiences, String jwksPath, String matchingClaimKey, String mapName,
      String identFileContents) throws Exception {
    try {
      setJWTConfigAndRestartCluster(
          allowedIssuers, allowedAudiences, jwksPath, matchingClaimKey, mapName, identFileContents);
      fail("Expected exception but did not get any.");
    } catch (PSQLException e) {
      // We cannot expect any specific error message here since we get different error messages on
      // Mac and Linux.
      // Error message on Linux: "The connection attempt failed".
      // Error message on Mac: "Connection to <host> refused. Check that the hostname and port are
      // correct and that the postmaster is accepting TCP/IP connections."
    } finally {
      // Mark the cluster for recreation so that the next test is not affected.
      markClusterNeedsRecreation();
    }
  }

  @Test
  public void invalidJWTAudiencesConfiguration() throws Exception {
    // Empty Audiences.
    assertClusterRestartFailure(ALLOWED_ISSUERS, Arrays.asList(), jwksPath,
        /* matchingClaimKey */ "",
        /* mapName */ "", /* identFileContents */ "");
  }

  @Test
  public void invalidJWTIssuersConfiguration() throws Exception {
    // Empty Issuers.
    assertClusterRestartFailure(Arrays.asList(), ALLOWED_AUDIENCES, jwksPath,
        /* matchingClaimKey */ "",
        /* mapName */ "", /* identFileContents */ "");
  }
}

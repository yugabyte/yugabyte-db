// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import java.security.KeyPair;
import java.time.Instant;
import java.util.Date;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.mvc.Http.Request;
import play.mvc.Http.RequestBuilder;

@RunWith(MockitoJUnitRunner.class)
public class JWTVerifierTest {
  @Mock private NodeAgentHandler handler;
  @Mock private JWTKeyProvider keyProvider;
  private JWTVerifier verfier;
  private KeyPair keyPair;

  @Before
  public void setup() throws Exception {
    verfier = new JWTVerifier(keyProvider);
    keyPair = CertificateHelper.getKeyPairObject();
  }

  @Test
  public void testAuthenticatorSuccess() {
    UUID nodeAgentUuid = UUID.randomUUID();
    UUID userUuid = UUID.randomUUID();
    String jwt =
        Jwts.builder()
            .setIssuer("https://www.yugabyte.com")
            .setSubject(ClientType.NODE_AGENT.name())
            .setIssuedAt(Date.from(Instant.now()))
            .setExpiration(Date.from(Instant.now().plusSeconds(600)))
            .claim(JWTVerifier.CLIENT_ID_CLAIM.toString(), nodeAgentUuid.toString())
            .claim(JWTVerifier.USER_ID_CLAIM.toString(), userUuid.toString())
            .signWith(SignatureAlgorithm.RS256, keyPair.getPrivate())
            .compact();
    Request request = new RequestBuilder().header(TokenAuthenticator.API_JWT_HEADER, jwt).build();
    when(keyProvider.getKey(any(), eq(ClientType.NODE_AGENT), eq(nodeAgentUuid), any()))
        .thenReturn(keyPair.getPublic());
    verfier.verify(request, TokenAuthenticator.API_JWT_HEADER);
    verify(keyProvider, times(1))
        .getKey(any(), eq(ClientType.NODE_AGENT), eq(nodeAgentUuid), any());
  }

  @Test
  public void testAuthenticatorMissingNodeUuidFailure() {
    String jwt =
        Jwts.builder()
            .setIssuer("https://www.yugabyte.com")
            .setSubject(ClientType.NODE_AGENT.name())
            .setIssuedAt(Date.from(Instant.now()))
            .setExpiration(Date.from(Instant.now().plusSeconds(600)))
            .signWith(SignatureAlgorithm.RS256, keyPair.getPrivate())
            .compact();
    Request request = new RequestBuilder().header(TokenAuthenticator.API_JWT_HEADER, jwt).build();
    Exception exception =
        assertThrows(
            PlatformServiceException.class,
            () -> verfier.verify(request, TokenAuthenticator.API_JWT_HEADER));
    assertThat(exception.getMessage(), containsString("Invalid token"));
    verify(keyProvider, never()).getKey(any(), any(), any(), any());
  }

  @Test
  public void testAuthenticatorWrongKeyFailure() throws Exception {
    KeyPair newKeyPair = CertificateHelper.getKeyPairObject();
    UUID nodeAgentUuid = UUID.randomUUID();
    UUID userUuid = UUID.randomUUID();
    // Return a different public key.
    String jwt =
        Jwts.builder()
            .setIssuer("https://www.yugabyte.com")
            .setSubject(ClientType.NODE_AGENT.name())
            .setIssuedAt(Date.from(Instant.now()))
            .setExpiration(Date.from(Instant.now().plusSeconds(600)))
            .claim(JWTVerifier.CLIENT_ID_CLAIM.toString(), nodeAgentUuid.toString())
            .claim(JWTVerifier.USER_ID_CLAIM.toString(), userUuid.toString())
            .signWith(SignatureAlgorithm.RS256, newKeyPair.getPrivate())
            .compact();
    Request request = new RequestBuilder().header(TokenAuthenticator.API_JWT_HEADER, jwt).build();
    when(keyProvider.getKey(any(), eq(ClientType.NODE_AGENT), eq(nodeAgentUuid), any()))
        .thenReturn(keyPair.getPublic());
    Exception exception =
        assertThrows(
            PlatformServiceException.class,
            () -> verfier.verify(request, TokenAuthenticator.API_JWT_HEADER));
    assertThat(exception.getMessage(), containsString("Invalid token"));
    verify(keyProvider, times(1))
        .getKey(any(), eq(ClientType.NODE_AGENT), eq(nodeAgentUuid), any());
  }
}

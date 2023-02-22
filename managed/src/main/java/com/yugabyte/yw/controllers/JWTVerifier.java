// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformServiceException;
import io.jsonwebtoken.Claims;
import io.jsonwebtoken.JwsHeader;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import io.jsonwebtoken.SigningKeyResolver;
import java.security.Key;
import java.util.EnumSet;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Request;
import play.mvc.Http.Status;

@Singleton
@Slf4j
public class JWTVerifier {
  public static final String CLIENT_TYPE_CLAIM = "clientType";
  public static final String CLIENT_ID_CLAIM = "clientId";
  public static final String USER_ID_CLAIM = "userId";

  private final JWTKeyProvider keyProvider;

  public enum ClientType {
    NODE_AGENT;

    public static Optional<ClientType> maybeResolve(String name) {
      return EnumSet.allOf(ClientType.class)
          .stream()
          .filter(e -> e.name().equalsIgnoreCase(name))
          .findFirst();
    }
  }

  @Inject
  public JWTVerifier(JWTKeyProvider keyProvider) {
    this.keyProvider = keyProvider;
  }

  private SigningKeyResolver getSigningKeyResolver(Request request, String jwt) {
    return new SigningKeyResolver() {
      @Override
      public Key resolveSigningKey(JwsHeader header, Claims claims) {
        SignatureAlgorithm algo = SignatureAlgorithm.forName(header.getAlgorithm());
        Optional<ClientType> clientTypeOp = ClientType.maybeResolve((String) claims.getSubject());
        if (!clientTypeOp.isPresent()) {
          log.error("Client type is not set");
          throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid token");
        }
        String clientId = (String) claims.get(CLIENT_ID_CLAIM);
        if (StringUtils.isBlank(clientId)) {
          log.error("Client ID is not set");
          throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid token");
        }
        UUID clientUuid = UUID.fromString(clientId);
        log.trace(
            "Getting JWT provider key for client type {} and client ID {}",
            clientTypeOp.get(),
            clientUuid);
        Key key = keyProvider.getKey(algo, clientTypeOp.get(), clientUuid, claims);
        if (key == null) {
          log.error("Error in getting for claims");
          throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid token");
        }
        // Store the client details.
        RequestContext.put(CLIENT_TYPE_CLAIM, clientTypeOp.get());
        RequestContext.put(CLIENT_ID_CLAIM, clientUuid);
        return key;
      }

      @Override
      public Key resolveSigningKey(JwsHeader header, String plaintext) {
        throw new UnsupportedOperationException();
      }
    };
  }

  @VisibleForTesting
  UUID verify(Request request, String header) {
    Optional<String> authTokenOp = request.header(header);
    if (authTokenOp.isPresent()) {
      try {
        String jwt = authTokenOp.get();
        Claims claims =
            Jwts.parser()
                .setSigningKeyResolver(getSigningKeyResolver(request, jwt))
                .parseClaimsJws(jwt)
                .getBody();
        String userId = (String) claims.get(USER_ID_CLAIM);
        if (StringUtils.isNotBlank(userId)) {
          UUID userUuid = UUID.fromString(userId);
          RequestContext.put(USER_ID_CLAIM, userUuid);
          return userUuid;
        }
      } catch (PlatformServiceException e) {
        throw e;
      } catch (RuntimeException e) {
        log.error("Fail to authenticate", e);
        throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid token");
      }
    }
    return null;
  }
}

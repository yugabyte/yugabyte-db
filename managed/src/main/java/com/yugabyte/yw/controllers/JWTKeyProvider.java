// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import io.jsonwebtoken.Claims;
import io.jsonwebtoken.SignatureAlgorithm;
import java.security.Key;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class JWTKeyProvider {
  private final NodeAgentHandler nodeAgentHandler;

  @Inject
  public JWTKeyProvider(NodeAgentHandler nodeAgentHandler) {
    this.nodeAgentHandler = nodeAgentHandler;
  }

  public Key getKey(
      SignatureAlgorithm algo, ClientType clientType, UUID clientUuid, Claims claims) {
    // Add key providers as needed.
    if (clientType == ClientType.NODE_AGENT && algo.isRsa() && clientUuid != null) {
      return nodeAgentHandler.getNodeAgentPublicKey(clientUuid);
    }
    log.error(
        "No JWT key provider found for client type {} and client ID {}", clientType, clientUuid);
    return null;
  }
}

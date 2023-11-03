// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import io.jsonwebtoken.Claims;
import io.jsonwebtoken.SignatureAlgorithm;
import java.security.Key;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class JWTKeyProvider {
  public Key getKey(
      SignatureAlgorithm algo, ClientType clientType, UUID clientUuid, Claims claims) {
    // Add key providers as needed.
    if (clientType == ClientType.NODE_AGENT && algo.isRsa() && clientUuid != null) {
      return NodeAgentManager.getNodeAgentPublicKey(clientUuid);
    }
    log.error(
        "No JWT key provider found for client type {} and client ID {}", clientType, clientUuid);
    return null;
  }
}

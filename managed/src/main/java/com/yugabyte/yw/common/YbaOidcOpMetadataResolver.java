// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.nimbusds.oauth2.sdk.auth.ClientAuthenticationMethod;
import com.nimbusds.openid.connect.sdk.op.OIDCProviderMetadata;
import java.util.List;
import org.pac4j.oidc.config.OidcConfiguration;
import org.pac4j.oidc.metadata.OidcOpMetadataResolver;

/**
 * Resolves the OP metadata from the given JSON, or from the discovery URI when it is null. Picks
 * client_secret_post over client_secret_basic before pac4j builds the token endpoint auth.
 */
public class YbaOidcOpMetadataResolver extends OidcOpMetadataResolver {

  private final OIDCProviderMetadata staticMetadata;

  public YbaOidcOpMetadataResolver(
      OidcConfiguration configuration, OIDCProviderMetadata staticMetadata) {
    super(configuration);
    this.staticMetadata = staticMetadata;
  }

  @Override
  protected OIDCProviderMetadata retrieveMetadata() {
    OIDCProviderMetadata metadata =
        staticMetadata != null ? staticMetadata : super.retrieveMetadata();
    List<ClientAuthenticationMethod> methods = metadata.getTokenEndpointAuthMethods();
    if (methods != null) {
      if (methods.contains(ClientAuthenticationMethod.CLIENT_SECRET_POST)) {
        configuration.setClientAuthenticationMethod(ClientAuthenticationMethod.CLIENT_SECRET_POST);
      } else if (methods.contains(ClientAuthenticationMethod.CLIENT_SECRET_BASIC)) {
        configuration.setClientAuthenticationMethod(ClientAuthenticationMethod.CLIENT_SECRET_BASIC);
      }
    }
    return metadata;
  }
}

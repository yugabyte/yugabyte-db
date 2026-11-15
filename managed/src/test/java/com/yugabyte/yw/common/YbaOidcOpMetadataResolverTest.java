// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.nimbusds.oauth2.sdk.auth.ClientAuthenticationMethod;
import com.nimbusds.oauth2.sdk.auth.ClientSecretBasic;
import com.nimbusds.oauth2.sdk.auth.ClientSecretPost;
import com.nimbusds.openid.connect.sdk.op.OIDCProviderMetadata;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Test;
import org.pac4j.oidc.config.OidcConfiguration;

public class YbaOidcOpMetadataResolverTest {

  private static String metadataJson(String authMethods) {
    return "{\"issuer\":\"https://idp.example.com\","
        + "\"authorization_endpoint\":\"https://idp.example.com/authorize\","
        + "\"token_endpoint\":\"https://idp.example.com/token\","
        + "\"jwks_uri\":\"https://idp.example.com/jwks\","
        + "\"response_types_supported\":[\"code\"],"
        + "\"subject_types_supported\":[\"public\"],"
        + "\"id_token_signing_alg_values_supported\":[\"RS256\"]"
        + (authMethods == null ? "" : ",\"token_endpoint_auth_methods_supported\":" + authMethods)
        + "}";
  }

  private static OidcConfiguration newConfiguration() {
    OidcConfiguration configuration = new OidcConfiguration();
    configuration.setClientId("yba");
    configuration.setSecret("secret");
    return configuration;
  }

  private static YbaOidcOpMetadataResolver initStaticResolver(
      OidcConfiguration configuration, String authMethods) throws Exception {
    YbaOidcOpMetadataResolver resolver =
        new YbaOidcOpMetadataResolver(
            configuration, OIDCProviderMetadata.parse(metadataJson(authMethods)));
    resolver.init();
    return resolver;
  }

  @Test
  public void testPrefersPostOverProviderOrder() throws Exception {
    OidcConfiguration configuration = newConfiguration();
    YbaOidcOpMetadataResolver resolver =
        initStaticResolver(configuration, "[\"client_secret_basic\",\"client_secret_post\"]");
    assertEquals(
        ClientAuthenticationMethod.CLIENT_SECRET_POST,
        configuration.getClientAuthenticationMethod());
    assertTrue(resolver.getClientAuthenticationTokenEndpoint() instanceof ClientSecretPost);
  }

  @Test
  public void testFallsBackToBasic() throws Exception {
    OidcConfiguration configuration = newConfiguration();
    YbaOidcOpMetadataResolver resolver =
        initStaticResolver(configuration, "[\"private_key_jwt\",\"client_secret_basic\"]");
    assertEquals(
        ClientAuthenticationMethod.CLIENT_SECRET_BASIC,
        configuration.getClientAuthenticationMethod());
    assertTrue(resolver.getClientAuthenticationTokenEndpoint() instanceof ClientSecretBasic);
  }

  @Test
  public void testNoAuthMethodsInMetadataKeepsPac4jDefault() throws Exception {
    OidcConfiguration configuration = newConfiguration();
    YbaOidcOpMetadataResolver resolver = initStaticResolver(configuration, null);
    assertNull(configuration.getClientAuthenticationMethod());
    assertTrue(resolver.getClientAuthenticationTokenEndpoint() instanceof ClientSecretBasic);
  }

  @Test
  public void testDiscoveryUriPrefersPost() throws Exception {
    Path discovery = Files.createTempFile("oidc-discovery", ".json");
    try {
      Files.writeString(
          discovery, metadataJson("[\"client_secret_basic\",\"client_secret_post\"]"));
      OidcConfiguration configuration = newConfiguration();
      configuration.setDiscoveryURI("file:" + discovery);
      YbaOidcOpMetadataResolver resolver = new YbaOidcOpMetadataResolver(configuration, null);
      resolver.init();
      assertEquals("https://idp.example.com", resolver.load().getIssuer().getValue());
      assertTrue(resolver.getClientAuthenticationTokenEndpoint() instanceof ClientSecretPost);
    } finally {
      Files.deleteIfExists(discovery);
    }
  }
}

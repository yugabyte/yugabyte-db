// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.nimbusds.oauth2.sdk.token.BearerAccessToken;
import com.nimbusds.oauth2.sdk.token.RefreshToken;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Users;
import java.time.Duration;
import java.util.Optional;
import java.util.UUID;
import org.apache.pekko.actor.Cancellable;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.oidc.profile.OidcProfile;
import org.springframework.test.util.ReflectionTestUtils;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class RefetchOIDCAccessTokenTest {

  private static final String TOKEN_ENDPOINT = "https://idp.example.com/token";

  @Test
  public void testRefreshStoresTokenReadableByPac4j() {
    PlatformScheduler scheduler = mock(PlatformScheduler.class);
    when(scheduler.schedule(anyString(), any(), any(), any())).thenReturn(mock(Cancellable.class));
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(GlobalConfKeys.oidcRefreshTokenInterval))
        .thenReturn(Duration.ofMinutes(5));
    when(confGetter.getGlobalConf(GlobalConfKeys.oidcEmailAttribute)).thenReturn("");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybSecuritySecret)).thenReturn("secret");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybClientID)).thenReturn("client");
    when(confGetter.getGlobalConf(GlobalConfKeys.oidcRefreshTokenEndpoint))
        .thenReturn(TOKEN_ENDPOINT);

    OidcProfile profile = new OidcProfile();
    profile.setAccessToken(new BearerAccessToken("old-token", 3600, null));
    profile.setRefreshToken(new RefreshToken("refresh-1"));
    ProfileManager profileManager = mock(ProfileManager.class);
    when(profileManager.getProfile(CommonProfile.class)).thenReturn(Optional.of(profile));
    Users user = mock(Users.class);
    when(user.getUuid()).thenReturn(UUID.randomUUID());

    RefetchOIDCAccessToken refetch =
        new RefetchOIDCAccessToken(scheduler, confGetter, mock(WSClientRefresher.class));
    refetch.start(profileManager, user);
    ApiHelper apiHelper = mock(ApiHelper.class);
    when(apiHelper.postRequestEncodedData(eq(TOKEN_ENDPOINT), anyString(), anyMap()))
        .thenReturn(Json.parse("{\"access_token\": \"new-token\", \"expires_in\": 3600}"));
    ReflectionTestUtils.setField(refetch, "apiHelper", apiHelper);

    ArgumentCaptor<Runnable> runner = ArgumentCaptor.forClass(Runnable.class);
    verify(scheduler).schedule(anyString(), any(), any(), runner.capture());
    runner.getValue().run();

    // ThirdPartyLoginHandler reads the token through getAccessToken().
    assertEquals("Bearer new-token", profile.getAccessToken().toAuthorizationHeader());
    verify(profileManager).save(true, profile, true);
  }
}

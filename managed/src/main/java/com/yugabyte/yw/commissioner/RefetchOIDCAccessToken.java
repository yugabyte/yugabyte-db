// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Users;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.actor.Cancellable;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;

@Singleton
@Slf4j
public class RefetchOIDCAccessToken {

  private final String YB_PLAY_WS_CONFIG_KEY = "yb.ws";

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final WSClientRefresher wsClientRefresher;
  private ApiHelper apiHelper;
  private Map<UUID, Cancellable> scheduledTasks = new ConcurrentHashMap<>();

  @Inject
  public RefetchOIDCAccessToken(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      WSClientRefresher wsClientRefresher) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.wsClientRefresher = wsClientRefresher;
  }

  public void start(ProfileManager<CommonProfile> profileManager, Users user) {
    this.apiHelper = retApiHelper();
    Duration refreshAccessTokenInterval = this.refreshTokenInterval();
    if (scheduledTasks.get(user.getUuid()) == null) {
      Cancellable scheduledTask =
          platformScheduler.schedule(
              getClass().getSimpleName(),
              refreshAccessTokenInterval,
              refreshAccessTokenInterval,
              () -> scheduleRunner(profileManager, user));
      scheduledTasks.put(user.getUuid(), scheduledTask);
    }
  }

  public void stop(Users user) {
    if (scheduledTasks.get(user.getUuid()) != null) {
      log.debug("Terminating refetch auth token task for user {}", user.getUuid());
      Cancellable scheduledTask = scheduledTasks.get(user.getUuid());
      if (!scheduledTask.isCancelled()) {
        scheduledTask.cancel();
        scheduledTasks.remove(user.getUuid());
      }
    }
  }

  private Duration refreshTokenInterval() {
    return confGetter.getGlobalConf(GlobalConfKeys.oidcRefreshTokenInterval);
  }

  private void scheduleRunner(ProfileManager<CommonProfile> profileManager, Users user) {
    CommonProfile profile = profileManager.get(true).get();
    try {
      if (profile.getAttribute("expiration") != null) {
        JsonNode data = refetchAccessToken(profile);
        String refreshedToken = data.get("access_token").asText();
        profile.addAttribute("access_token", refreshedToken);
        Date updatedDate = this.computeExpiryTime(data.get("expires_in").asLong());
        profile.addAttribute("expiration", updatedDate);
        profileManager.save(true, profile, true);
      }
    } catch (Exception e) {
      log.debug("Refreshing access_token failed for user {}. Terminating the task", user.getUuid());
      this.stop(user);
    }
  }

  private JsonNode refetchAccessToken(CommonProfile profile) {
    if (profile.getAttribute("refresh_token") != null) {
      String emailAttr = confGetter.getGlobalConf(GlobalConfKeys.oidcEmailAttribute);
      String email;
      if (emailAttr.equals("")) {
        email = profile.getEmail();
      } else {
        email = (String) profile.getAttribute(emailAttr);
      }
      log.debug("Re-fetching the access token for user {}", email);
      String refreshTokenEndpoint = confGetter.getGlobalConf(GlobalConfKeys.ybSecuritySecret);
      if (refreshTokenEndpoint == null) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "The Refresh Token endpoint is not configured. Please configure it through the UI as"
                + " part of the OIDC (OpenID Connect) configuration.");
      }
      Map<String, String> formData = new HashMap<>();
      formData.put("grant_type", "refresh_token");
      formData.put("refresh_token", profile.getAttribute("refresh_token").toString());
      formData.put("client_secret", confGetter.getGlobalConf(GlobalConfKeys.ybSecuritySecret));
      formData.put("client_id", confGetter.getGlobalConf(GlobalConfKeys.ybClientID));

      // Convert formData to a URL-encoded string
      String urlEncodedData =
          formData.entrySet().stream()
              .map(
                  entry ->
                      entry.getKey()
                          + "="
                          + URLEncoder.encode(entry.getValue(), StandardCharsets.UTF_8))
              .collect(Collectors.joining("&"));
      Map<String, String> headers = new HashMap<>();
      headers.put("Content-Type", "application/x-www-form-urlencoded");
      headers.put("Accept", "application/json");

      JsonNode data =
          this.apiHelper.postRequestEncodedData(
              confGetter.getGlobalConf(GlobalConfKeys.oidcRefreshTokenEndpoint),
              urlEncodedData,
              headers);
      final JsonNode errors = data.get("error");
      if (errors != null) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errors.toString());
      }
      return data;
    }

    return null;
  }

  private Date computeExpiryTime(long expiresIn) {
    // Calculate the expiration time relative to the current timestamp
    Instant currentTimestamp = Instant.now();
    Instant expiryInstant = currentTimestamp.plusSeconds(expiresIn);
    return Date.from(expiryInstant);
  }

  private ApiHelper retApiHelper() {
    return new ApiHelper(wsClientRefresher.getClient(YB_PLAY_WS_CONFIG_KEY));
  }
}

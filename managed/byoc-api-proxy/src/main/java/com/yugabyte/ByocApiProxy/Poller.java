package com.yugabyte.ByocApiProxy;

import java.util.UUID;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.core.env.Environment;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Slf4j
@Component
public class Poller {
  private final String proxiedAppBasePath;
  private final String ybaBasePath;
  private final UUID ybaId;

  @Autowired
  public Poller(Environment env) {
    proxiedAppBasePath = env.getProperty("proxied_app.base_url");

    if (proxiedAppBasePath == null) {
      throw new IllegalArgumentException("proxied_app.base_url property is required");
    }

    ybaBasePath = env.getProperty("yba.base_url");

    if (ybaBasePath == null) {
      throw new IllegalArgumentException("yba.base_url property is required");
    }

    String ybaIdProperty = env.getProperty("yba.uuid");

    if (ybaIdProperty == null) {
      throw new IllegalArgumentException("yba.uuid property is required");
    }

    ybaId = UUID.fromString(ybaIdProperty);
  }

  @Scheduled(fixedDelay = 1L, timeUnit = TimeUnit.SECONDS)
  public void run() {
    // TODO:
    //    authenticate to the API
    //    query the pending requests
    //    dispatch to worker threads
    //    post results
    log.info(
        "Poller started against YBA {} ({}) and API Server {}",
        ybaId,
        ybaBasePath,
        proxiedAppBasePath);
  }
}

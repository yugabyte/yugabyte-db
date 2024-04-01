package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.Release;

public class ReleaseContainerFactory {

  @Inject CloudUtilFactory cloudUtilFactory;
  @Inject Config appConfig;
  @Inject ReleasesUtils releasesUtils;

  public ReleaseContainer newReleaseContainer(ReleaseMetadata metadata) {
    return new ReleaseContainer(metadata, cloudUtilFactory, appConfig, releasesUtils);
  }

  public ReleaseContainer newReleaseContainer(Release release) {
    return new ReleaseContainer(release, cloudUtilFactory, appConfig, releasesUtils);
  }
}

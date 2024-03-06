package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.Release;

public class ReleaseContainerFactory {

  @Inject CloudUtilFactory cloudUtilFactory;

  public ReleaseContainer newReleaseContainer(ReleaseMetadata metadata) {
    return new ReleaseContainer(metadata, cloudUtilFactory);
  }

  public ReleaseContainer newReleaseContainer(Release release) {
    return new ReleaseContainer(release, cloudUtilFactory);
  }
}

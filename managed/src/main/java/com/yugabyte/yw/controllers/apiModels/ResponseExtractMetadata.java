package com.yugabyte.yw.controllers.apiModels;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import java.util.Date;
import java.util.UUID;

public class ResponseExtractMetadata {
  public UUID metadata_uuid;
  public String version;
  public Release.YbType yb_type;
  public String sha256;
  public ReleaseArtifact.Platform platform;
  public Architecture architecture;
  public String release_type;
  public Date release_date;
  public String release_notes;

  public static enum Status {
    waiting,
    running,
    success,
    failure
  }

  public Status status;
}

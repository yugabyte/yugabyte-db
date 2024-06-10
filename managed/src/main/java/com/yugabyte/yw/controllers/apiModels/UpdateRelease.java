package com.yugabyte.yw.controllers.apiModels;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.models.ReleaseArtifact;
import io.swagger.annotations.ApiModel;
import java.util.List;
import java.util.UUID;
import play.data.validation.Constraints;

@ApiModel(description = "Release metadata required to create a new release")
public class UpdateRelease {
  public static class Artifact {
    public UUID package_file_id;
    public String package_url;
    @Constraints.Required public ReleaseArtifact.Platform platform;
    @Constraints.Required public PublicCloudConstants.Architecture architecture;
    public String sha256;
  }

  public List<Artifact> artifacts;

  public Long release_date;
  public String release_notes;
  public String release_tag;
  public String state;
}

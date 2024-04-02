package com.yugabyte.yw.controllers.apiModels;

import java.util.List;
import java.util.UUID;

public class ResponseRelease {
  public UUID releaseUuid;
  public String version;
  public String yb_type;
  public String releaseType;

  public static class Artifact {
    public UUID packageFileID;
    public String fileName;
    public String packageURL;
  }

  public List<Artifact> artifacts;

  public String releaseDate;
  public String releaseNotes;
  public String releaseTag;
}

// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;


import com.google.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;

import javax.inject.Singleton;
import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Predicate;
import java.util.stream.Collectors;

@Singleton
public class ReleaseManager {

  @Inject
  ConfigHelper configHelper;

  @Inject
  Configuration appConfig;

  final ConfigHelper.ConfigType CONFIG_TYPE = ConfigHelper.ConfigType.SoftwareReleases;

  public static final Logger LOG = LoggerFactory.getLogger(ReleaseManager.class);
  final PathMatcher ybPackageMatcher =
      FileSystems.getDefault().getPathMatcher("glob:**yugabyte*.tar.gz");
  Predicate<Path> packagesFilter = p -> Files.isRegularFile(p) && ybPackageMatcher.matches(p);

  public Map<String, String> getLocalReleases(String localReleasePath) {
    Map<String, String> releaseMap = new HashMap<>();

    try {
      // Return a map of version# and software package
      releaseMap = Files.walk(Paths.get(localReleasePath))
                   .filter(packagesFilter)
                   .collect(Collectors.toMap(
                      p -> p.getName(p.getNameCount() - 2).toString(),
                      p -> p.toAbsolutePath().toString()
                  ));
    } catch (IOException e) {
      LOG.error(e.getMessage());
    }
    return releaseMap;
  }

  public void loadReleasesToDB() {
    // TODO: also have way to pull S3 based releases based on a flag.
    String ybReleasesPath = appConfig.getString("yb.releases.path");
    if (ybReleasesPath != null && ybReleasesPath.length() > 0) {
      Map<String, String> releaseMap = getLocalReleases(ybReleasesPath);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, (Map) releaseMap);
    }
  }

  public String getReleaseByVersion(String version) {
    return getReleases().get(version);
  }

  public Map<String, String> getReleases() {
    return (Map) configHelper.getConfig(CONFIG_TYPE);
  }
}

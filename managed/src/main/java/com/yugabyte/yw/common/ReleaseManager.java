// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;


import com.google.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;

import javax.inject.Singleton;
import java.io.File;
import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;

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

  final Pattern ybPackagePattern = Pattern.compile("[^.]+yugabyte-ee-(.*)-centos(.*).tar.gz");

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
    if (ybReleasesPath != null && !ybReleasesPath.isEmpty()) {
      moveDockerReleaseFile(ybReleasesPath);
      Map<String, String> releaseMap = getLocalReleases(ybReleasesPath);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, (Map) releaseMap);
    }
  }

  /**
   * This method would move the yugabyte server package that we bundle with docker image
   * to yb releases path.
   *
   * @param ybReleasesPath (str): Yugabyte releases path to create the move the files to.
   */
  private void moveDockerReleaseFile(String ybReleasesPath) {
    String ybDockerRelease = appConfig.getString("yb.docker.release");
    if (ybDockerRelease == null || ybDockerRelease.isEmpty()) {
      return;
    }

    try {
      Files.walk(Paths.get(ybDockerRelease))
        .map(String::valueOf).map(ybPackagePattern::matcher)
        .filter(Matcher::matches)
        .forEach(match -> {
          File releaseFile = new File(match.group());
          File destinationFolder = new File(ybReleasesPath, match.group(1));
          File destinationFile = new File(destinationFolder, releaseFile.getName());
          if (!destinationFolder.exists()) {
            destinationFolder.mkdir();
          }
          try {
            Files.move(releaseFile.toPath(), destinationFile.toPath(), REPLACE_EXISTING);
          } catch (IOException e) {
            throw new RuntimeException("Unable to move docker release file " +
                releaseFile.toPath() + " to " + destinationFile);
          }
        });
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to look up release files in " + ybDockerRelease);
    }
  }

  public String getReleaseByVersion(String version) {
    return getReleases().get(version);
  }

  public Map<String, String> getReleases() {
    return (Map) configHelper.getConfig(CONFIG_TYPE);
  }
}

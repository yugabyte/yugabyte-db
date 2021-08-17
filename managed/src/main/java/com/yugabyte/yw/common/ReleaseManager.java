// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;

import com.google.inject.Inject;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.File;
import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.libs.Json;

@Singleton
public class ReleaseManager {

  @Inject ConfigHelper configHelper;

  @Inject Configuration appConfig;

  public enum ReleaseState {
    ACTIVE,
    DISABLED,
    DELETED
  }

  final ConfigHelper.ConfigType CONFIG_TYPE = ConfigHelper.ConfigType.SoftwareReleases;

  @ApiModel(description = "Release data")
  public static class ReleaseMetadata {

    @ApiModelProperty(value = "Release state", example = "ACTIVE")
    public ReleaseState state = ReleaseState.ACTIVE;

    @ApiModelProperty(value = "Release notes")
    public List<String> notes;

    // File path where the release binary is stored
    @ApiModelProperty(value = "Release file path")
    public String filePath;

    // File path where the release helm chart is stored
    @ApiModelProperty(value = "Helm chart path")
    public String chartPath;

    // Docker image tag corresponding to the release
    @ApiModelProperty(value = "Release image tag")
    public String imageTag;

    public static ReleaseMetadata fromLegacy(String version, Object metadata) {
      // Legacy release metadata would have name and release path alone
      // convert those to new format.
      ReleaseMetadata rm = create(version);
      rm.filePath = (String) metadata;
      rm.chartPath = "";
      return rm;
    }

    public static ReleaseMetadata create(String version) {
      ReleaseMetadata rm = new ReleaseMetadata();
      rm.state = ReleaseState.ACTIVE;
      rm.imageTag = version;
      rm.notes = new ArrayList<>();
      return rm;
    }

    public ReleaseMetadata withFilePath(String filePath) {
      this.filePath = filePath;
      return this;
    }

    public ReleaseMetadata withChartPath(String chartPath) {
      this.chartPath = chartPath;
      return this;
    }

    public String toString() {
      return getClass().getName()
          + " state: "
          + String.valueOf(state)
          + " filePath: "
          + String.valueOf(filePath)
          + " chartPath: "
          + String.valueOf(chartPath)
          + " imageTag: "
          + String.valueOf(imageTag);
    }
  }

  public static final Logger LOG = LoggerFactory.getLogger(ReleaseManager.class);

  final PathMatcher ybPackageMatcher =
      FileSystems.getDefault().getPathMatcher("glob:**yugabyte*centos*.tar.gz");
  final Predicate<Path> ybPackageFilter =
      p -> Files.isRegularFile(p) && ybPackageMatcher.matches(p);

  final PathMatcher ybChartMatcher =
      FileSystems.getDefault().getPathMatcher("glob:**yugabyte-*-helm.tar.gz");
  final Predicate<Path> ybChartFilter = p -> Files.isRegularFile(p) && ybChartMatcher.matches(p);

  // This regex needs to support old style packages with -ee as well as new style packages without.
  // There are previously existing YW deployments that will have the old packages and users will
  // need to still be able to use said universes and their existing YB releases.
  final Pattern ybPackagePattern = Pattern.compile("[^.]+yugabyte-(?:ee-)?(.*)-centos(.*).tar.gz");

  final Pattern ybHelmChartPattern = Pattern.compile("[^.]+yugabyte-(.*)-helm.tar.gz");

  public Map<String, String> getReleaseFiles(String releasesPath, Predicate<Path> fileFilter) {
    Map<String, String> fileMap = new HashMap<>();
    try {
      fileMap =
          Files.walk(Paths.get(releasesPath))
              .filter(fileFilter)
              .collect(
                  Collectors.toMap(
                      p -> p.getName(p.getNameCount() - 2).toString(),
                      p -> p.toAbsolutePath().toString()));
    } catch (IOException e) {
      LOG.error(e.getMessage());
    }
    return fileMap;
  }

  public Map<String, ReleaseMetadata> getLocalReleases(String releasesPath) {
    Map<String, ReleaseMetadata> localReleases = new HashMap<>();
    Map<String, String> releaseFiles = getReleaseFiles(releasesPath, ybPackageFilter);
    Map<String, String> releaseCharts = getReleaseFiles(releasesPath, ybChartFilter);
    releaseFiles.forEach(
        (version, filePath) -> {
          ReleaseMetadata r =
              ReleaseMetadata.create(version)
                  .withFilePath(filePath)
                  .withChartPath(releaseCharts.getOrDefault(version, ""));
          localReleases.put(version, r);
        });
    return localReleases;
  }

  public Map<String, Object> getReleaseMetadata() {
    Map<String, Object> releases = configHelper.getConfig(CONFIG_TYPE);
    if (releases == null || releases.isEmpty()) {
      LOG.debug("getReleaseMetadata: No releases found");
      return new HashMap<>();
    }
    releases.forEach(
        (version, metadata) -> {
          if (metadata instanceof String) {
            releases.put(version, ReleaseMetadata.fromLegacy(version, metadata));
          }
        });

    return releases;
  }

  public void addRelease(String version) {
    ReleaseMetadata metadata = ReleaseMetadata.create(version);
    addReleaseWithMetadata(version, metadata);
  }

  public void addReleaseWithMetadata(String version, ReleaseMetadata metadata) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    if (currentReleases.containsKey(version)) {
      throw new RuntimeException("Release already exists: " + version);
    }
    LOG.info("Adding release version {} with metadata {}", version, metadata.toString());
    currentReleases.put(version, metadata);
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
  }

  public void importLocalReleases() {
    String ybReleasesPath = appConfig.getString("yb.releases.path");
    String ybReleasePath = appConfig.getString("yb.docker.release");
    String ybHelmChartPath = appConfig.getString("yb.helm.packagePath");
    if (ybReleasesPath != null && !ybReleasesPath.isEmpty()) {
      moveFiles(ybReleasePath, ybReleasesPath, ybPackagePattern);
      moveFiles(ybHelmChartPath, ybReleasesPath, ybHelmChartPattern);
      Map<String, ReleaseMetadata> localReleases = getLocalReleases(ybReleasesPath);
      Map<String, Object> currentReleases = getReleaseMetadata();
      localReleases.keySet().removeAll(currentReleases.keySet());
      LOG.debug("Current releases: [ {} ]", currentReleases.keySet().toString());
      LOG.debug("Local releases: [ {} ]", localReleases.keySet());
      if (!localReleases.isEmpty()) {
        LOG.info("Importing local releases: [ {} ]", localReleases.toString());
        localReleases.forEach(currentReleases::put);
        configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
      }
    }
  }

  public void updateReleaseMetadata(String version, ReleaseMetadata newData) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    if (currentReleases.containsKey(version)) {
      currentReleases.put(version, newData);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
    }
  }

  /**
   * This method moves files that match a specific regex to a destination directory.
   *
   * @param sourceDir (str): Source directory to move files from
   * @param destinationDir (str): Destination directory to move files to
   * @param fileRegex (str): Regular expression specifying files to move
   */
  private static void moveFiles(String sourceDir, String destinationDir, Pattern fileRegex) {
    if (sourceDir == null || sourceDir.isEmpty()) {
      return;
    }

    try {
      Files.walk(Paths.get(sourceDir))
          .map(String::valueOf)
          .map(fileRegex::matcher)
          .filter(Matcher::matches)
          .forEach(
              match -> {
                File releaseFile = new File(match.group());
                File destinationFolder = new File(destinationDir, match.group(1));
                File destinationFile = new File(destinationFolder, releaseFile.getName());
                if (!destinationFolder.exists()) {
                  destinationFolder.mkdir();
                }
                try {
                  Files.move(releaseFile.toPath(), destinationFile.toPath(), REPLACE_EXISTING);
                } catch (IOException e) {
                  throw new RuntimeException(
                      "Unable to move release file "
                          + releaseFile.toPath()
                          + " to "
                          + destinationFile);
                }
              });
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to look up release files in " + sourceDir);
    }
  }

  public ReleaseMetadata getReleaseByVersion(String version) {
    Object metadata = getReleaseMetadata().get(version);
    if (metadata == null) {
      return null;
    }
    return Json.fromJson(Json.toJson(metadata), ReleaseMetadata.class);
  }

  public Map<String, String> getReleases() {
    return (Map) configHelper.getConfig(CONFIG_TYPE);
  }
}

// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;

import autovalue.shaded.com.google.common.collect.Sets;
import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.ReleaseFormData;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import javax.validation.Valid;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.StringUtils;
import play.Environment;
import play.data.validation.Constraints;
import play.libs.Json;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class ReleaseManager {

  public static final ConfigHelper.ConfigType CONFIG_TYPE =
      ConfigHelper.ConfigType.SoftwareReleases;
  public static final ConfigHelper.ConfigType YBC_CONFIG_TYPE =
      ConfigHelper.ConfigType.YbcSoftwareReleases;
  private static final String YB_PACKAGE_REGEX =
      "yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz";
  private static final String YB_RELEASES_PATH = "yb.releases.path";
  private static final String YBC_RELEASES_PATH = "ybc.releases.path";
  private static final String UPLOAD_ARTIFACT_PATH = "yb.releases.artifacts.upload_path";

  private final Pattern ybPackagePatternRequiredInChartPath =
      Pattern.compile("(.*)yugabyte-(?:ee-)?(.*)-(helm)(.*).tar.gz");
  private final Pattern ybVersionPatternRequired =
      Pattern.compile("^(\\d+.\\d+.\\d+(.\\d+)?)(-(b(\\d+)(-.+)?|(\\w+)))?$");

  private final ConfigHelper configHelper;
  private final Config appConfig;
  private final RuntimeConfGetter confGetter;
  private final Environment environment;
  private final ReleaseContainerFactory releaseContainerFactory;
  private final ReleasesUtils releasesUtils;

  @Inject
  public ReleaseManager(
      ConfigHelper configHelper,
      Config appConfig,
      RuntimeConfGetter confGetter,
      Environment environment,
      ReleaseContainerFactory releaseContainerFactory,
      ReleasesUtils releasesUtils) {
    this.configHelper = configHelper;
    this.appConfig = appConfig;
    this.confGetter = confGetter;
    this.environment = environment;
    this.releaseContainerFactory = releaseContainerFactory;
    this.releasesUtils = releasesUtils;
  }

  public enum ReleaseState {
    ACTIVE,
    DISABLED,
    DELETED
  }

  @ApiModel(description = "Yugabyte release metadata")
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

    @ApiModelProperty(value = "Release packages")
    public List<Package> packages;

    // Docker image tag corresponding to the release
    @ApiModelProperty(value = "Release image tag")
    public String imageTag;

    // S3 link and credentials for remote downloading of the release
    @ApiModelProperty(value = "S3 link and credentials")
    public S3Location s3;

    // GCS link and credentials for remote downloading of the release
    @ApiModelProperty(value = "GCS link and credentials")
    public GCSLocation gcs;

    // HTTP link for remote downloading of the release
    @ApiModelProperty(value = "HTTP link to the release")
    public HttpLocation http;

    @lombok.Getter
    @lombok.Setter
    public static class PackagePaths {
      @ApiModelProperty(value = "Path to x86_64 package")
      @Constraints.Pattern(
          message =
              "File path must be prefixed with s3://, gs://, or http(s)://,"
                  + " and must contain path to package file, instead of a directory",
          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      public String x86_64;

      @ApiModelProperty(value = "Checksum for x86_64 package")
      @JsonAlias("x86_64Checksum")
      public String x86_64_checksum;

      @ApiModelProperty(value = "Path to the Helm chart package")
      @Constraints.Pattern(
          message =
              "File path must be prefixed with s3://, gs://, or http(s)://,"
                  + " and must contain path to package file, instead of a directory",
          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      public String helmChart;

      @ApiModelProperty(value = "Checksum for the Helm chart package")
      public String helmChartChecksum;

      //      @ApiModelProperty(value = "Path to aarch64 package")
      //      @Constraints.Pattern(
      //          message = "Must be prefixed with s3:// or http(s)://",
      //          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      //      public String aarch64;
      //
      //      @ApiModelProperty(value = "Checksum for aarch64 package")
      //      @JsonAlias("aarch64_checksum")
      //      public String aarch64Checksum;
    }

    @lombok.Getter
    @lombok.Setter
    public static class S3Location {
      @ApiModelProperty(value = "package paths")
      @Constraints.Required
      @Valid
      public PackagePaths paths;

      // S3 credentials.
      @ApiModelProperty(value = "access key id", hidden = true)
      @Constraints.Required
      public String accessKeyId;

      @ApiModelProperty(value = "access key secret", hidden = true)
      @Constraints.Required
      public String secretAccessKey;
    }

    @lombok.Getter
    @lombok.Setter
    public static class GCSLocation {
      @ApiModelProperty(value = "package paths")
      @Constraints.Required
      @Valid
      public PackagePaths paths;

      // GCS credentials.
      @ApiModelProperty(value = "gcs service key json", hidden = true)
      @Constraints.Required
      public String credentialsJson;
    }

    @lombok.Getter
    @lombok.Setter
    public static class HttpLocation {
      @ApiModelProperty(value = "package paths")
      @Valid
      public PackagePaths paths;
    }

    public static class Package {
      @ApiModelProperty @Constraints.Required public String path;

      @ApiModelProperty @Constraints.Required public Architecture arch;
    }

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
      rm.packages = new ArrayList<>();
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

    public ReleaseMetadata withPackage(String path, Architecture arch) {
      Package p = new Package();
      p.path = path;
      p.arch = arch;
      this.packages.add(p);
      return this;
    }

    public String toString() {
      return Json.toJson(CommonUtils.maskObject(this)).toString();
    }

    private List<Package> matchPackages(Architecture arch) {
      // Old style release without packages. No matching packages.
      if (packages == null) {
        return Collections.emptyList();
      }
      return packages.stream().filter(p -> p.arch == arch).collect(Collectors.toList());
    }

    public String getFilePath(Region region) {
      Architecture arch = region.getArchitecture();
      return getFilePath(arch);
    }

    public String getFilePath(Architecture arch) {
      // Must be old style region or release with no architecture (or packages).
      if (arch == null || packages == null || packages.isEmpty()) {
        return filePath;
      }
      List<Package> matched = matchPackages(arch);
      if (matched.size() == 0) {
        throw new RuntimeException(
            "Could not find matching package with architecture " + arch.name());
      } else if (matched.size() > 1) {
        log.warn(
            "Found more than one package with matching architecture, picking {}.",
            matched.get(0).path);
      }
      return matched.get(0).path;
    }

    public Boolean matchesRegion(Region region) {
      Architecture arch = region.getArchitecture();
      // Must be old style region or release with no architecture (or packages).
      if (arch == null || packages == null || packages.isEmpty()) {
        return true;
      }
      List<Package> matched = matchPackages(arch);
      return matched.size() > 0;
    }

    public Boolean matchesArchitecture(Architecture arch) {
      List<Package> matched = matchPackages(arch);
      return matched.size() > 0;
    }

    // Returns true if the metadata contains at least one local release.
    @ApiModelProperty(value = "local release", hidden = true)
    @JsonIgnore
    public boolean hasLocalRelease() {
      if (packages == null || packages.isEmpty()) {
        return !(s3 != null || gcs != null || http != null);
      }
      return packages.stream().anyMatch(p -> p.path.startsWith("/"));
    }

    @JsonIgnore
    public Set<String> getLocalReleases() {
      return packages.stream()
          .map(p -> p.path)
          .filter(path -> path.startsWith("/"))
          .collect(Collectors.toSet());
    }
  }

  private Predicate<Path> getPackageFilter(String pathMatchGlob) {
    return p -> Files.isRegularFile(p) && getPathMatcher(pathMatchGlob).matches(p);
  }

  private PathMatcher getPathMatcher(String pathMatchGlob) {
    return FileSystems.getDefault().getPathMatcher(pathMatchGlob);
  }

  final Predicate<Path> ybChartFilter = getPackageFilter("glob:**yugabyte-*-helm.tar.gz");

  // This regex needs to support old style packages with -ee as well as new style packages without.
  // There are previously existing YW deployments that will have the old packages and users will
  // need to still be able to use said universes and their existing YB releases.
  private static final Pattern ybPackagePattern = Pattern.compile(YB_PACKAGE_REGEX);

  private static final Pattern ybHelmChartPattern = Pattern.compile("yugabyte-(.*).tgz");

  static final Pattern ybVersionPattern =
      Pattern.compile("(.*)(\\d+.\\d+.\\d+(.\\d+)?)(-(b(\\d+)(-.+)?|(\\w+)))?(.*)");

  // Similar to above but only matches local release /<releasesPath>/<version>/*
  static final Pattern ybLocalPattern =
      Pattern.compile("(/.*?)(/\\d+.\\d+.\\d+(.\\d+)?)(-(b(\\d+)(-.+)?|(\\w+)))?(.*)");

  private static final Pattern ybcPackagePattern =
      Pattern.compile("[^.]+ybc-(?:ee-)?(.*)-(linux|el8)(.*).tar.gz");

  public Map<String, String> getReleaseFiles(
      String releasesPath, Predicate<Path> fileFilter, boolean ybcRelease) {
    Map<String, String> fileMap = new HashMap<>();
    Set<String> duplicateKeys = new HashSet<>();
    try {
      Files.walk(Paths.get(releasesPath))
          .filter(fileFilter)
          .forEach(
              p -> {
                // In case of ybc release, we want to store osType, archType in version key.
                String key =
                    ybcRelease
                        ? StringUtils.removeEnd(
                            p.getName(p.getNameCount() - 1).toString(), ".tar.gz")
                        : p.getName(p.getNameCount() - 2).toString();
                String value = p.toAbsolutePath().toString();
                if (!fileMap.containsKey(key)) {
                  fileMap.put(key, value);
                } else if (!duplicateKeys.contains(key)) {
                  log.warn(
                      String.format(
                          "Skipping %s - it contains multiple releases of same architecture type",
                          key));
                  duplicateKeys.add(key);
                }
              });
      duplicateKeys.forEach(k -> fileMap.remove(k));
    } catch (IOException e) {
      log.error(e.getMessage());
    }
    return fileMap;
  }

  private void updateLocalReleases(
      Map<String, ReleaseMetadata> localReleases,
      Map<String, String> releaseFiles,
      Map<String, String> releaseCharts,
      Architecture arch) {
    releaseFiles.forEach(
        (version, filePath) -> {
          ReleaseMetadata r = localReleases.get(version);
          if (r == null) {
            r =
                ReleaseMetadata.create(version)
                    .withFilePath(filePath)
                    .withChartPath(releaseCharts.getOrDefault(version, ""));
          }
          localReleases.put(version, r.withPackage(filePath, arch));
        });
  }

  public Map<String, ReleaseMetadata> getLocalReleases() {
    return getLocalReleases(appConfig.getString(YB_RELEASES_PATH));
  }

  public Map<String, ReleaseMetadata> getLocalReleases(String releasesPath) {
    Map<String, String> releaseFiles;
    Map<String, String> releaseCharts = getReleaseFiles(releasesPath, ybChartFilter, false);
    Map<String, ReleaseMetadata> localReleases = new HashMap<>();
    for (Architecture arch : Architecture.values()) {
      releaseFiles = getReleaseFiles(releasesPath, getPackageFilter(arch.getDBGlob()), false);
      updateLocalReleases(localReleases, releaseFiles, releaseCharts, arch);
    }
    return localReleases;
  }

  public List<String> getLocalReleaseVersions() {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      return new ArrayList<String>(getLocalReleases().keySet());
    } else {
      // Get the version of every release that has at least 1 "ReleaseLocalFile" artifact
      return Release.getAll().stream()
          .filter(r -> ReleaseArtifact.getForReleaseLocalFile(r.getReleaseUUID()).size() != 0)
          .map(r -> r.getVersion())
          .collect(Collectors.toList());
    }
  }

  public Map<String, ReleaseMetadata> getLocalYbcReleases(String releasesPath) {
    Map<String, String> releaseFiles;
    Map<String, ReleaseMetadata> localReleases = new HashMap<>();
    for (Architecture arch : Architecture.values()) {
      releaseFiles = getReleaseFiles(releasesPath, getPackageFilter(arch.getYbcGlob()), true);
      updateLocalReleases(localReleases, releaseFiles, new HashMap<>(), arch);
    }
    return localReleases;
  }

  public Map<String, Object> getReleaseMetadata() {
    return getReleaseMetadata(CONFIG_TYPE);
  }

  public Map<String, Object> getReleaseMetadata(ConfigHelper.ConfigType configType) {
    Map<String, Object> releases = configHelper.getConfig(configType);
    if (releases == null || releases.isEmpty()) {
      log.debug("getReleaseMetadata: No releases found");
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

  /**
   * It enforces the following two conditions: 1. Proper formatting of the .tar.gz package name. 2.
   * Proper formatting of the DB version in the .tar.gz package name. It also checks and prints a
   * warning for equality of the DB version in the .tar.gz package name with the DB version
   * specified in the Version field.
   *
   * @param version The YBDB version that the package belongs to
   * @param packageName The name to check
   */
  public static void verifyPackageNameFormat(String version, String packageName) {
    Matcher ybPackagePatternMatcher = ybPackagePattern.matcher(packageName);
    Matcher versionPatternMatcher = ybVersionPattern.matcher(packageName);
    if (!ybPackagePatternMatcher.find()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The package name of the .tar.gz file is improperly formatted. Please"
              + " check to make sure that you have typed in the package name correctly.");
    }
    if (!versionPatternMatcher.find()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The version of DB in your package name is improperly formatted. Please"
              + " check to make sure that you have typed in the DB version correctly"
              + " in the package name.");
    }
    if (!packageName.contains(version)) {
      log.warn("Package {} might not belong to version {}", packageName, version);
    }
  }

  public static void verifyPackageNameFormat(String version, ReleaseMetadata.PackagePaths paths) {
    if (paths.x86_64 != null) {
      verifyPackageNameFormat(version, FilenameUtils.getName(paths.x86_64));
    }
    //    if (paths.aarch64 != null) {
    //      verifyPackageNameFormat(version, FilenameUtils.getName(paths.aarch64));
    //    }
    if (paths.helmChart != null) {
      Matcher helmChartPatternMatcher = ybHelmChartPattern.matcher(paths.helmChart);
      if (!helmChartPatternMatcher.find()) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "The package name of the helm chart is improperly formatted. Please"
                + " check to make sure that you have typed in the package name correctly.");
      }
    }
  }

  public static void verifyReleaseFormDataList(List<ReleaseFormData> releaseFormDataList) {
    releaseFormDataList.forEach(
        versionData -> {
          if (versionData.version == null) {
            throw new PlatformServiceException(Status.BAD_REQUEST, "Version is not specified");
          }
          // At least one link should be specified for each version.
          if (versionData.s3 == null && versionData.gcs == null && versionData.http == null) {
            throw new RuntimeException(
                "At least one of S3 link, GCS link, or HTTP link must be specified");
          }

          if (versionData.s3 != null) {
            verifyPackageNameFormat(versionData.version, versionData.s3.paths);
          }
          if (versionData.gcs != null) {
            verifyPackageNameFormat(versionData.version, versionData.gcs.paths);
          }
          if (versionData.http != null) {
            verifyPackageNameFormat(versionData.version, versionData.http.paths);
          }
        });
  }

  public static Map<String, ReleaseMetadata> formDataToReleaseMetadata(
      List<ReleaseFormData> versionDataList) {
    verifyReleaseFormDataList(versionDataList);

    Map<String, ReleaseMetadata> releases = new HashMap<>();
    for (ReleaseFormData versionData : versionDataList) {
      ReleaseMetadata metadata = ReleaseMetadata.create(versionData.version);

      if (versionData.s3 != null) {
        metadata.s3 = versionData.s3;
        metadata.filePath = metadata.s3.paths.x86_64;
      }

      if (versionData.gcs != null) {
        metadata.gcs = versionData.gcs;
        metadata.filePath = metadata.gcs.paths.x86_64;
      }

      if (versionData.http != null) {
        metadata.http = versionData.http;
        metadata.filePath = metadata.http.paths.x86_64;
      }
      releases.put(versionData.version, metadata);
    }
    return releases;
  }

  public void downloadYbHelmChart(String version, Release release) {
    ReleaseContainer rc = releaseContainerFactory.newReleaseContainer(release);
    downloadYbHelmChart(version, rc);
  }

  public void downloadYbHelmChart(String version, ReleaseMetadata metadata) {
    ReleaseContainer rc = releaseContainerFactory.newReleaseContainer(metadata);
    downloadYbHelmChart(version, rc);
  }

  public void downloadYbHelmChart(String version, ReleaseContainer release) {
    try {
      String ybReleasesPath = appConfig.getString(YB_RELEASES_PATH);
      release.downloadYbHelmChart(version, ybReleasesPath);
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Could not download the helm charts for version %s : %s", version, e.getMessage()),
          e);
    }
  }

  // We should only be hitting this when not using the redesign
  public synchronized void addReleaseWithMetadata(String version, ReleaseMetadata metadata) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    if (currentReleases.containsKey(version)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, String.format("Release already exists for version %s", version));
    }
    validateSoftwareVersionOnCurrentYbaVersion(version);
    log.info("Adding release version {} with metadata {}", version, metadata.toString());
    ReleaseContainer release = releaseContainerFactory.newReleaseContainer(metadata);
    downloadYbHelmChart(version, release);
    log.info("Metadata after helm chart download {}", metadata);
    currentReleases.put(version, metadata);

    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
  }

  public synchronized void removeRelease(String version) {
    Map<String, Object> currentReleases = getReleaseMetadata();

    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Release release = Release.getByVersion(version);
      if (release != null) {
        release.delete();
      }
    } else {
      if (currentReleases.containsKey(version)) {
        log.info("Removing release version {}", version);
        currentReleases.remove(version);
        configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
      }
    }

    // delete specific release's directory recursively.
    File releaseDirectory = new File(appConfig.getString(YB_RELEASES_PATH), version);
    FileUtils.deleteDirectory(releaseDirectory);
  }

  public synchronized void importLocalReleases() {
    String ybReleasePath = appConfig.getString("yb.docker.release");
    String ybHelmChartPath = appConfig.getString("yb.helm.packagePath");
    String ybReleasesPath = appConfig.getString(YB_RELEASES_PATH);
    if (ybReleasesPath != null && !ybReleasesPath.isEmpty()) {
      if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
        Set<String> currentVersions =
            Release.getAll().stream().map(r -> r.getVersion()).collect(Collectors.toSet());
        copyReleasesFromDockerRelease(ybReleasesPath, currentVersions);
        List<ReleaseLocalFile> currentLocalFiles = ReleaseLocalFile.getLocalFiles();
        Set<String> currentFilePaths =
            Sets.newHashSet(
                currentLocalFiles.stream()
                    .map(rlf -> rlf.getLocalFilePath())
                    .collect(Collectors.toList()));
        try {
          Files.walk(Paths.get(ybReleasesPath))
              .filter(
                  getPackageFilter(
                      "glob:**yugabyte*{centos,alma,linux,el}*{x86_64,aarch64}.tar.gz"))
              .filter(p -> !currentFilePaths.contains(p.toString())) // Filter files already known
              .forEach(
                  p -> {
                    ReleasesUtils.ExtractedMetadata metadata = null;
                    try {
                      log.debug("checking local file {}", p.toString());
                      metadata = releasesUtils.metadataFromPath(p);
                      String rawVersion = metadata.version.split("-")[0];
                      if (metadata.platform == ReleaseArtifact.Platform.KUBERNETES) {
                        localReleaseNameValidation(rawVersion, null, p.toString());
                      } else {
                        localReleaseNameValidation(rawVersion, p.toString(), null);
                      }
                    } catch (RuntimeException e) {
                      log.error(
                          "local release "
                              + p.getFileName().toString()
                              + " failed validation: "
                              + e.getLocalizedMessage());
                      // continue forEach
                      return;
                    }
                    ReleaseLocalFile rlf = ReleaseLocalFile.create(p.toString());
                    ReleaseArtifact artifact =
                        ReleaseArtifact.create(
                            metadata.sha256,
                            metadata.platform,
                            metadata.architecture,
                            rlf.getFileUUID());
                    Release release = Release.getByVersion(metadata.version, metadata.releaseTag);
                    if (release == null) {
                      release =
                          Release.create(
                              metadata.version, metadata.release_type, metadata.releaseTag);
                    }
                    release.addArtifact(artifact);
                  });
        } catch (Exception e) {
          log.error("failed to read local releases", e);
        }

      } else {
        importLocalLegacyReleases(ybReleasePath, ybHelmChartPath, ybReleasesPath);
      }
    }

    log.info("Starting ybc local releases");
    String ybcReleasePath = appConfig.getString("ybc.docker.release");
    String ybcReleasesPath = appConfig.getString(YBC_RELEASES_PATH);
    log.info("ybcReleasesPath: " + ybcReleasesPath);
    log.info("ybcReleasePath: " + ybcReleasePath);
    if (ybcReleasesPath != null && !ybcReleasesPath.isEmpty()) {
      Map<String, Object> currentYbcReleases = getReleaseMetadata(YBC_CONFIG_TYPE);
      File ybcReleasePathFile = new File(ybcReleasePath);
      File ybcReleasesPathFile = new File(ybcReleasesPath);
      if (ybcReleasePathFile.exists() && ybcReleasesPathFile.exists()) {
        // No need to skip copying packages as we want to allow multiple arch type of a ybc-version.
        copyFiles(ybcReleasePath, ybcReleasesPath, ybcPackagePattern, null);
        Map<String, ReleaseMetadata> localYbcReleases = getLocalYbcReleases(ybcReleasesPath);
        localYbcReleases.keySet().removeAll(currentYbcReleases.keySet());
        log.info("Current ybc releases: [ {} ]", currentYbcReleases.keySet().toString());
        log.info("Local ybc releases: [ {} ]", localYbcReleases.keySet().toString());
        if (!localYbcReleases.isEmpty()) {
          log.info("Importing local releases: [ {} ]", Json.toJson(localYbcReleases));
          currentYbcReleases.putAll(localYbcReleases);
          configHelper.loadConfigToDB(YBC_CONFIG_TYPE, currentYbcReleases);
        }
      } else {
        log.warn(
            "ybc release dir: {} and/or ybc releases dir: {} not present",
            ybcReleasePath,
            ybcReleasesPath);
      }
    }
  }

  private void importLocalLegacyReleases(
      String ybReleasePath, String ybHelmChartPath, String ybReleasesPath) {
    Map<String, Object> currentReleases = getReleaseMetadata();

    // Local copy pattern to account for the presence of characters prior to the file name itself.
    // (ensures that all local releases get imported prior to version checking).
    copyReleasesFromDockerRelease(ybReleasesPath, currentReleases.keySet());
    Map<String, ReleaseMetadata> localReleases = getLocalReleases(ybReleasesPath);
    localReleases.keySet().removeAll(currentReleases.keySet());
    log.debug("Current releases: [ {} ]", currentReleases.keySet().toString());
    log.debug("Local releases: [ {} ]", localReleases.keySet());

    // As described in the diff, we don't touch the currrent releases that have already been
    // imported. We
    // perform the same checks that we did in the import dialog case here prior to the import).

    // If there is an error for one release, there is still a possibility that the user has
    // imported multiple
    // releases locally, and that the other ones have been named properly. We skip the release
    // with the error, and continue with the other releases.

    if (!localReleases.isEmpty()) {
      Map<String, ReleaseMetadata> successfullyAddedReleases =
          new HashMap<String, ReleaseMetadata>();

      for (String version : localReleases.keySet()) {
        try {
          localReleaseNameValidation(
              version, localReleases.get(version).filePath, localReleases.get(version).chartPath);
        } catch (RuntimeException e) {
          log.error("local release " + version + " failed validation", e);
          continue;
        }
        // Release has been added successfully.
        successfullyAddedReleases.put(version, localReleases.get(version));
      }

      log.info("Importing local releases: [ {} ]", Json.toJson(successfullyAddedReleases));
      successfullyAddedReleases.forEach(currentReleases::put);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
    }
  }

  private void localReleaseNameValidation(String version, String filePath, String chartPath) {
    Matcher versionPatternMatcher = ybVersionPatternRequired.matcher(version);
    if (!versionPatternMatcher.find()) {

      throw new RuntimeException(
          "The version name in the folder of the imported local release is improperly "
              + "formatted. Please check to make sure that the folder with the version"
              + " name is named correctly.");
    }

    if (filePath != null) {
      String filePackageName = filePath.split("/")[(filePath.split("/")).length - 1];
      Matcher packagePatternFileMatcher = ybPackagePattern.matcher(filePackageName);
      Matcher versionPatternMatcherInPackageNameFilePath =
          ybVersionPattern.matcher(filePackageName);
      if (!versionPatternMatcherInPackageNameFilePath.find()) {

        throw new RuntimeException(
            "In the file path, the version of DB in your package name in the imported "
                + "local release is improperly formatted. Please "
                + " check to make sure that you have named the .tar.gz file with "
                + " the appropriate DB version.");
      }

      if (filePackageName != null && !filePackageName.contains(version)) {

        throw new RuntimeException(
            "The version of DB that you have specified in the folder name in the "
                + "imported local release does not match the version of DB in the "
                + "package name in the imported local release (specifed through the "
                + "file path). Please make sure that you have named the directory and "
                + ".tar.gz file appropriately so that the DB version in the package "
                + "name matches the DB version in the folder name.");
      }
    }
    if (chartPath != null) {
      String chartPackageName = chartPath.split("/")[(chartPath.split("/")).length - 1];
      Matcher packagePatternChartMatcher =
          ybPackagePatternRequiredInChartPath.matcher(chartPackageName);

      Matcher versionPatternMatcherInPackageNameChartPath =
          ybVersionPattern.matcher(chartPackageName);
      if (!chartPath.equals("")) {

        if (!versionPatternMatcherInPackageNameChartPath.find()) {

          throw new RuntimeException(
              "In the chart path, the version of DB in your package name in the imported "
                  + "local release is improperly formatted. Please "
                  + " check to make sure that you have named the .tar.gz file with "
                  + " the appropriate DB version.");
        }

        if (!chartPackageName.contains(version)) {

          throw new RuntimeException(
              "The version of DB that you have specified in the folder name in the "
                  + "imported local release does not match the version of DB in the "
                  + "package name in the imported local release (specifed through the "
                  + "chart path). Please make sure that you have named the directory and "
                  + ".tar.gz file appropriately so that the DB version in the package "
                  + "name matches the DB version in the folder name.");
        }
      }
    }
  }

  public void findLatestArmRelease(String currentVersion) {
    // currentVersion - 2.17.2.0-PRE_RELEASE
    JsonNode releaseTree;
    try {
      URL dockerUrl =
          new URL("https://registry.hub.docker.com/v2/repositories/yugabytedb/yugabyte/tags");
      ObjectMapper mapper = new ObjectMapper();
      releaseTree = mapper.readTree(dockerUrl);
    } catch (Exception e) {
      log.warn(
          String.format(
              "Error reading release tags from URL. Skipping ARM http release import. %s",
              e.getMessage()));
      return;
    }

    Comparator<JsonNode> releaseNameComparator =
        new Comparator<JsonNode>() {
          @Override
          public int compare(JsonNode r1, JsonNode r2) {
            // Compare r2 to r1 because we want latest elements first.
            return Util.compareYbVersions(r2.get("name").asText(), r1.get("name").asText());
          }
        };

    if (releaseTree != null && releaseTree.has("results")) {
      List<JsonNode> releases = releaseTree.findParents("name");
      if (releases == null || releases.isEmpty()) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Could not find versions in response JSON.");
      }
      boolean isCurrentVersionStable = Util.isStableVersion(currentVersion, false);
      // Get the latest stable is current is stable and latest preview if current is preview.
      JsonNode latestRelease =
          releases.stream()
              .filter(r -> Util.isYbVersionFormatValid(r.get("name").asText()))
              // Filter only the latest stable versions if current version is stable, and vice versa
              // for preview.
              .filter(
                  r -> {
                    boolean isVersionStable = Util.isStableVersion(r.get("name").asText(), false);
                    return isCurrentVersionStable ? isVersionStable : !isVersionStable;
                  })
              .filter(r -> Util.compareYbVersions(currentVersion, r.get("name").asText()) >= 0)
              .sorted(releaseNameComparator)
              .findFirst()
              .get();
      if (latestRelease == null || latestRelease.isNull()) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST, "Could not find latest release in response JSON.");
      }
      // version format - 2.17.1.0-b439
      String version = latestRelease.get("name").asText();
      String httpsUrl =
          String.format(
              "https://downloads.yugabyte.com/releases/%s/yugabyte-%s-el8-aarch64.tar.gz",
              version.split("-")[0], version);
      addHttpsRelease(version, httpsUrl, Architecture.aarch64, "%s-https-aarch64");
    }
  }

  private synchronized void addHttpsRelease(
      String version, String url, Architecture arch, String versionFormatString) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Map<String, Object> currentReleases = getReleaseMetadata();
      ReleaseMetadata rm;
      if (currentReleases.containsKey(version)) {
        // merge https URL with existing metadata
        rm = metadataFromObject(currentReleases.get(version));
        if (rm.http == null) {
          ReleaseMetadata.PackagePaths httpsPaths = new ReleaseMetadata.PackagePaths();
          httpsPaths.x86_64 = url;
          verifyPackageNameFormat(version, httpsPaths);
          rm.http = new ReleaseMetadata.HttpLocation();
          rm.http.paths = httpsPaths;
          rm = rm.withPackage(url, arch);
        }
      } else {
        // create new release with only https ARM URL
        verifyPackageNameFormat(version, url);
        version = String.format(versionFormatString, version);
        rm = ReleaseMetadata.create(version).withFilePath(url).withPackage(url, arch);
        ReleaseMetadata.PackagePaths httpsPaths = new ReleaseMetadata.PackagePaths();
        httpsPaths.x86_64 = url;
        rm.http = new ReleaseMetadata.HttpLocation();
        rm.http.paths = httpsPaths;
      }
      updateReleaseMetadata(version, rm);
    } else {
      ReleasesUtils.ExtractedMetadata em;
      try {
        em = releasesUtils.versionMetadataFromURL(new URL(url));
      } catch (Exception e) {
        log.error("failed to parse url " + url, e);
        return;
      }

      Release release = Release.getByVersion(version);
      if (release == null) {
        release = Release.create(version, em.release_type);
      }
      if (release.getArtifactForArchitecture(arch) == null) {
        ReleaseArtifact artifact =
            ReleaseArtifact.create("", ReleaseArtifact.Platform.LINUX, arch, url);
        release.addArtifact(artifact);
      }
    }
  }

  // Idempotent method to replace all Software and Ybc filepaths with the new config paths.
  public synchronized void fixFilePaths() {
    // Fix SoftwareReleases.
    String ybReleasesPath = appConfig.getString(YB_RELEASES_PATH);
    String uploadFilePath = appConfig.getString(UPLOAD_ARTIFACT_PATH);
    Map<String, Object> softwareReleases = getReleaseMetadata(CONFIG_TYPE);
    Map<String, Object> updatedReleases = new HashMap<>();
    softwareReleases.forEach(
        (version, object) -> {
          ReleaseMetadata rm = metadataFromObject(object);
          // Only fix up non remote paths
          if (rm.hasLocalRelease()) {
            try {
              rm.filePath =
                  FileDataService.fixFilePath(ybLocalPattern, rm.filePath, ybReleasesPath);
            } catch (Exception e) {
              log.warn("Error {} replacing release {}. Skipping.", e.getMessage(), rm.filePath);
            }

            // Fix up packages list.
            if (rm.packages != null && !rm.packages.isEmpty()) {
              rm.packages.forEach(
                  p -> {
                    try {
                      p.path = FileDataService.fixFilePath(ybLocalPattern, p.path, ybReleasesPath);
                    } catch (Exception e) {
                      log.warn(
                          "Error {} replacing packages release {}. Skipping.",
                          e.getMessage(),
                          p.path);
                    }
                  });
            }
          }
          // Fix up chart path.
          try {
            rm.chartPath =
                FileDataService.fixFilePath(ybLocalPattern, rm.chartPath, ybReleasesPath);
          } catch (Exception e) {
            log.warn("Error {} replacing chart {}. Skipping.", e.getMessage(), rm.chartPath);
          }
          updatedReleases.put(version, rm);
        });
    configHelper.loadConfigToDB(CONFIG_TYPE, updatedReleases);

    // Fix any ReleaseLocalFiles
    ReleaseLocalFile.getAll()
        .forEach(
            rlf -> {
              String basePath = ybReleasesPath;
              if (rlf.isUpload()) {
                basePath = uploadFilePath;
              }
              try {
                rlf.setLocalFilePath(
                    FileDataService.fixFilePath(ybLocalPattern, rlf.getLocalFilePath(), basePath));
              } catch (Exception e) {
                log.warn(
                    "Error {} replacing release local file {}. Skipping.",
                    e.getMessage(),
                    rlf.getLocalFilePath());
              }
            });

    // Fix YbcReleases.
    String ybcReleasesPath = appConfig.getString(YBC_RELEASES_PATH);
    Map<String, Object> ybcReleases = getReleaseMetadata(YBC_CONFIG_TYPE);
    Map<String, Object> updatedYbcReleases = new HashMap<>();
    ybcReleases.forEach(
        (version, object) -> {
          ReleaseMetadata rm = metadataFromObject(object);
          try {
            rm.filePath = FileDataService.fixFilePath(ybLocalPattern, rm.filePath, ybcReleasesPath);
          } catch (Exception e) {
            log.warn("Error {} replacing ybc release {}. Skipping.", e.getMessage(), rm.filePath);
          }
          if (rm.packages != null && !rm.packages.isEmpty()) {
            rm.packages.forEach(
                p -> {
                  try {
                    p.path = FileDataService.fixFilePath(ybLocalPattern, p.path, ybcReleasesPath);
                  } catch (Exception e) {
                    log.warn(
                        "Error {} replacing packages ybc release {}. Skipping.",
                        e.getMessage(),
                        p.path);
                  }
                });
          }
          updatedYbcReleases.put(version, rm);
        });
    configHelper.loadConfigToDB(YBC_CONFIG_TYPE, updatedYbcReleases);
  }

  // Idempotent method to update all software releases with packages if possible.
  public synchronized void updateCurrentReleases() {
    // Only needed for legacy releases.
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Map<String, Object> currentReleases = getReleaseMetadata();
      Map<String, Object> updatedReleases = new HashMap<>();
      currentReleases.forEach(
          (version, object) -> {
            ReleaseMetadata rm = metadataFromObject(object);
            // update packages if possible
            if ((rm.packages == null || rm.packages.isEmpty())
                && !(rm.filePath == null || rm.filePath.isEmpty())) {
              Path fp = null;
              try {
                fp = Paths.get(rm.filePath);
              } catch (InvalidPathException e) {
                log.error("Error {} getting package path for version {}", e.getMessage(), version);
              }
              if (fp != null) {
                for (Architecture arch : Architecture.values()) {
                  if (getPathMatcher(arch.getDBGlob()).matches(fp)) {
                    rm.packages = (rm.packages == null) ? new ArrayList<>() : rm.packages;
                    rm = rm.withPackage(rm.filePath, arch);
                  }
                }
              }
              if (rm.packages == null || rm.packages.isEmpty()) {
                log.warn(
                    "Could not match any available architectures to existing release {}", version);
              }
            }
            updatedReleases.put(version, rm);
          });
      configHelper.loadConfigToDB(CONFIG_TYPE, updatedReleases);
    } else {
      log.debug("skipping updateCurrentReleases for new release decoupling");
    }
  }

  public synchronized InputStream getTarGZipDBPackageInputStream(String version) throws Exception {
    ReleaseContainer rc = getReleaseByVersion(version);
    return rc.getTarGZipDBPackageInputStream();
  }

  // Adds metadata to releases is version does not exist. Updates existing value if key present.
  public synchronized void updateReleaseMetadata(String version, ReleaseMetadata newData) {
    validateSoftwareVersionOnCurrentYbaVersion(version);
    Map<String, Object> currentReleases = getReleaseMetadata();
    currentReleases.put(version, newData);
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
  }

  private void copyReleasesFromDockerRelease(String destinationDir, Set<String> skipVersions) {
    String ybReleasePath = appConfig.getString("yb.docker.release");
    String ybHelmChartPath = appConfig.getString("yb.helm.packagePath");
    Pattern ybPackagePatternCopy =
        Pattern.compile(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex));
    Pattern ybHelmChartPatternCopy =
        Pattern.compile(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex));
    copyFiles(ybReleasePath, destinationDir, ybPackagePatternCopy, skipVersions);
    copyFiles(ybHelmChartPath, destinationDir, ybHelmChartPatternCopy, skipVersions);
  }

  /**
   * This method copies release files that match a specific regex to a destination directory.
   *
   * @param sourceDir (str): Source directory to move files from
   * @param destinationDir (str): Destination directory to move files to
   * @param fileRegex (str): Regular expression specifying files to move
   * @param skipVersions : Set of versions to ignore while copying. version is the first matching
   *     group from fileRegex
   */
  private static void copyFiles(
      String sourceDir, String destinationDir, Pattern fileRegex, Set<String> skipVersions) {
    if (sourceDir == null || sourceDir.isEmpty()) {
      return;
    }

    try {
      Files.walk(Paths.get(sourceDir))
          .map(String::valueOf)
          .map(path -> path.substring(sourceDir.length()))
          .map(fileRegex::matcher)
          .filter(Matcher::matches)
          .forEach(
              match -> {
                String version = match.group(1);
                if (skipVersions != null && skipVersions.contains(version)) {
                  log.debug("Skipping re-copy of release files for {}", version);
                  return;
                }
                File releaseFile = new File(sourceDir + match.group());
                File destinationFolder = new File(destinationDir, version);
                File destinationFile = new File(destinationFolder, releaseFile.getName());
                if (!destinationFolder.exists()) {
                  destinationFolder.mkdir();
                }
                try {
                  Files.copy(releaseFile.toPath(), destinationFile.toPath(), REPLACE_EXISTING);
                } catch (IOException e) {
                  throw new RuntimeException(
                      "Unable to copy release file "
                          + releaseFile.toPath()
                          + " to "
                          + destinationFile);
                }
              });
    } catch (IOException e) {
      log.error(e.getMessage());
      throw new RuntimeException("Unable to look up release files in " + sourceDir);
    }
  }

  public ReleaseContainer getReleaseByVersion(String version) {
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Release release = Release.getByVersion(version);
      if (release == null) {
        return null;
      }
      return releaseContainerFactory.newReleaseContainer(Release.getByVersion(version));
    } else {
      Object metadata = getReleaseMetadata().get(version);
      if (metadata == null) {
        return null;
      }
      return releaseContainerFactory.newReleaseContainer(metadataFromObject(metadata));
    }
  }

  public Map<String, ReleaseContainer> getAllReleaseContainersByVersion() {
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      return Release.getAll().stream()
          .collect(
              Collectors.toMap(
                  release ->
                      release.getReleaseTag() == null
                          ? release.getVersion()
                          : String.format("%s-%s", release.getVersion(), release.getReleaseTag()),
                  release -> releaseContainerFactory.newReleaseContainer(release)));
    } else {
      return getReleaseMetadata().entrySet().stream()
          .collect(
              Collectors.toMap(
                  entry -> entry.getKey(),
                  entry ->
                      releaseContainerFactory.newReleaseContainer(
                          metadataFromObject(entry.getValue()))));
    }
  }

  public ReleaseMetadata getYbcReleaseByVersion(String version, String osType, String archType) {
    version = String.format("ybc-%s-%s-%s", version, osType, archType);
    Object metadata = getReleaseMetadata(YBC_CONFIG_TYPE).get(version);
    if (metadata == null) {
      log.error(String.format("ybc version %s not found", version));
      return null;
    }
    return metadataFromObject(metadata);
  }

  public ReleaseMetadata metadataFromObject(Object object) {
    return Json.fromJson(Json.toJson(object), ReleaseMetadata.class);
  }

  public Map<String, String> getReleases() {
    return (Map) configHelper.getConfig(CONFIG_TYPE);
  }

  public boolean getInUse(String version) {
    return Universe.existsRelease(version);
  }

  private void validateSoftwareVersionOnCurrentYbaVersion(String ybSoftwareVersion) {
    if (confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion)) {
      return;
    }

    if (confGetter.getGlobalConf(GlobalConfKeys.skipVersionChecks)) {
      return;
    }

    String currentYbaVersion = ConfigHelper.getCurrentVersion(environment);

    int cmpValue;
    String msg;
    try {
      cmpValue = Util.compareYbVersions(ybSoftwareVersion, currentYbaVersion);
    } catch (RuntimeException e) {
      msg =
          String.format(
              "There was a problem while comparing requested DB version "
                  + "%s with YBA version %s, not proceeding.",
              ybSoftwareVersion, currentYbaVersion);
      throw new PlatformServiceException(Status.BAD_REQUEST, msg);
    }
    if (cmpValue > 0) {
      msg =
          String.format(
              "DB version %s, a version higher than current YBA Version %s is not supported",
              ybSoftwareVersion, currentYbaVersion);
      throw new PlatformServiceException(Status.BAD_REQUEST, msg);
    }
  }
}

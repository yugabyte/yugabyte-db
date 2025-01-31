// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.dataformat.xml.JacksonXmlModule;
import com.fasterxml.jackson.dataformat.xml.XmlMapper;
import com.fasterxml.jackson.dataformat.xml.annotation.JacksonXmlElementWrapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeSet;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import javax.inject.Singleton;
import lombok.EqualsAndHashCode;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;

@Singleton
public class GFlagsValidation {

  private final Environment environment;

  private final RuntimeConfGetter confGetter;

  private final ReleaseManager releaseManager;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidation.class);

  public static final String YSQL_MIGRATION_FILES_LIST_FILE_NAME = "ysql_migration_files_list.json";

  public static final String MASTER_GFLAG_FILE_NAME = "master_flags.xml";

  public static final String TSERVER_GFLAG_FILE_NAME = "tserver_flags.xml";

  private static final String GLIBC_VERSION_FIELD_NAME = "glibc_v";

  private static final String YSQL_MAJOR_VERSION_FIELD_NAME = "ysql_major_version";

  // Skip these test auto flags while computing auto flags in YBA.
  public static final Set<String> TEST_AUTO_FLAGS =
      ImmutableSet.of("TEST_auto_flags_new_install", "TEST_auto_flags_initialized");

  public static final List<String> GFLAG_FILENAME_LIST =
      ImmutableList.of(
          MASTER_GFLAG_FILE_NAME,
          TSERVER_GFLAG_FILE_NAME,
          Util.AUTO_FLAG_FILENAME,
          Util.DB_VERSION_METADATA_FILENAME,
          YSQL_MIGRATION_FILES_LIST_FILE_NAME);

  public static final String DB_BUILD_WITH_FLAG_FILES = "2.16.0.0-b1";

  @Inject
  public GFlagsValidation(
      Environment environment, RuntimeConfGetter confGetter, ReleaseManager releaseManager) {
    this.environment = environment;
    this.confGetter = confGetter;
    this.releaseManager = releaseManager;
  }

  /**
   * Given a YBDB version, returns a set of JsonPaths containing the sensitive Master and Tserver
   * Gflags.
   */
  public Set<String> getSensitiveJsonPathsForVersion(String version) {
    LOG.info("Parsing sensitive gflags for DB version " + version);
    Set<String> sensitiveGflags = new HashSet<>();
    for (ServerType server : ServerType.values()) {
      if (!server.equals(ServerType.MASTER) && !server.equals(ServerType.TSERVER)) {
        continue;
      }
      try {
        for (GFlagDetails gflag : extractGFlags(version, server.name(), false)) {
          if (gflag.tags.contains("sensitive_info")) {
            sensitiveGflags.add("$.." + gflag.name);
          }
        }
      } catch (Exception e) {
        LOG.error("Error while fetching Gflags for db version " + version, e);
      }
    }
    return sensitiveGflags;
  }

  public List<GFlagDetails> extractGFlags(String version, String serverType, boolean mostUsedGFlags)
      throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    File file =
        new File(
            String.format("%s/%s/%s_flags.xml", releasesPath, version, serverType.toLowerCase()));
    InputStream flagStream = null;
    try {
      if (Files.exists(Paths.get(file.getAbsolutePath()))) {
        flagStream = FileUtils.getInputStreamOrFail(file);
      } else if (CommonUtils.isReleaseEqualOrAfter(DB_BUILD_WITH_FLAG_FILES, version)) {
        // Fetch gFlags files from DB package if it does not exist
        try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
          String gFlagFileName =
              serverType.equals(ServerType.MASTER.name())
                  ? MASTER_GFLAG_FILE_NAME
                  : TSERVER_GFLAG_FILE_NAME;
          fetchGFlagFilesFromTarGZipInputStream(
              inputStream, version, Collections.singletonList(gFlagFileName), releasesPath);
          flagStream = FileUtils.getInputStreamOrFail(file);
        } catch (Exception e) {
          LOG.error("Error in extracting flags from DB package", e);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Error in extracting flags form DB package");
        }
      } else {
        // Use static flag files for old versions.
        String majorVersion = version.substring(0, StringUtils.ordinalIndexOf(version, ".", 2));
        flagStream =
            environment.resourceAsStream(
                "gflags_metadata/" + majorVersion + "/" + serverType.toLowerCase() + ".xml");
        if (flagStream == null) {
          LOG.error("GFlags metadata file for " + majorVersion + " is not present");
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "GFlags metadata file for " + majorVersion + " is not present");
        }
      }
      JacksonXmlModule xmlModule = new JacksonXmlModule();
      xmlModule.setDefaultUseWrapper(false);
      XmlMapper xmlMapper = new XmlMapper(xmlModule);
      AllGFlags data = xmlMapper.readValue(flagStream, AllGFlags.class);
      if (mostUsedGFlags) {
        InputStream inputStream =
            environment.resourceAsStream("gflags_metadata/most_used_gflags.json");
        ObjectMapper mapper = new ObjectMapper();
        MostUsedGFlags freqUsedGFlags = mapper.readValue(inputStream, MostUsedGFlags.class);
        List<GFlagDetails> result = new ArrayList<>();
        for (GFlagDetails flag : data.flags) {
          if (serverType.equals(ServerType.MASTER.name())) {
            if (freqUsedGFlags.masterGFlags.contains(flag.name)) {
              result.add(flag);
            }
          } else {
            if (freqUsedGFlags.tserverGFlags.contains(flag.name)) {
              result.add(flag);
            }
          }
        }
        return result;
      }
      return data.flags;
    } finally {
      if (flagStream != null) {
        flagStream.close();
      }
    }
  }

  public Optional<GFlagDetails> getGFlagDetails(String version, String serverType, String gflagName)
      throws IOException {
    List<GFlagDetails> gflagsList = extractGFlags(version, serverType, false);
    return gflagsList.stream().filter(flag -> flag.name.equals(gflagName)).findFirst();
  }

  public List<GFlagGroup> extractGFlagGroups(String version) throws IOException {
    InputStream flagStream = null;
    try {
      SortedSet<String> avaliableVersions = new TreeSet<>();
      InputStream foldersStream = environment.resourceAsStream("gflag_groups");
      try (BufferedReader in = new BufferedReader(new InputStreamReader(foldersStream))) {
        String inputLine;
        while ((inputLine = in.readLine()) != null) {
          avaliableVersions.add(inputLine);
        }
      }
      SortedSet<String> head = avaliableVersions.headSet(version);
      if (head.isEmpty()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Failed to find gflags group version for " + version + " db");
      }
      String versionToUse = head.last();
      LOG.debug(
          "Found {} group versions, picked {} for current db version {}",
          avaliableVersions,
          versionToUse,
          version);
      flagStream =
          environment.resourceAsStream(
              "gflag_groups/" + versionToUse + "/" + Util.GFLAG_GROUPS_FILENAME);
      if (flagStream == null) {
        LOG.error("GFlag groups metadata file for " + versionToUse + " is not present");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "GFlag groups metadata file for " + versionToUse + " is not present");
      }
      ObjectMapper mapper = new ObjectMapper();
      List<GFlagGroup> data =
          mapper.readValue(flagStream, new TypeReference<List<GFlagGroup>>() {});
      return data;
    } finally {
      if (flagStream != null) {
        flagStream.close();
      }
    }
  }

  public void validateConnectionPoolingGflags(
      Universe universe, Map<String, String> connectionPoolingGflags) {
    if (connectionPoolingGflags.isEmpty()) {
      return;
    }

    // Get the right connection pooling gflags list for preview vs stable version.
    String ybdbVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isStableVersion = Util.isStableVersion(ybdbVersion, false);

    String flagsFileName = "connection_pooling/connection_pooling_gflags_";
    if (isStableVersion) {
      flagsFileName += "stable.json";
    } else {
      flagsFileName += "preview.json";
    }

    // Get the connection pooling gflags file content.
    ObjectNode flagsFileObject = null;
    try {
      ObjectMapper mapper = new ObjectMapper();
      flagsFileObject = (ObjectNode) mapper.readTree(environment.resourceAsStream(flagsFileName));
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error occurred retrieving connection pooling gflags file '%s'.", flagsFileName);
      LOG.error(errMsg, e);
      // If error with reading the file, log error and continue the operation.
      return;
    }

    // Find the correct version to check against.
    // Find the closest version less than or equal to ybdbVersion.
    SortedSet<String> avaliableVersions = new TreeSet<>();
    flagsFileObject.fieldNames().forEachRemaining(avaliableVersions::add);
    SortedSet<String> head = avaliableVersions.headSet(ybdbVersion);
    if (head.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Failed to find connection pooling flags for '" + ybdbVersion + "' db version.");
    }
    String versionToUse = head.last();
    LOG.debug(
        "Found '{}' connection pooling flags versions, picked '{}' for current db version '{}'.",
        avaliableVersions,
        versionToUse,
        ybdbVersion);

    // Get all the allowed connection pooling gflags for that version.
    JsonNode versionNode = flagsFileObject.get(versionToUse);
    Set<String> allowedGflagsForCurrentVersion =
        StreamSupport.stream(versionNode.spliterator(), false)
            .map(JsonNode::asText)
            .collect(Collectors.toSet());

    // If there are extra gflags not related to connection pooling for that DB version, throw an
    // error. Else validation is successful.
    List<String> invalidConnectionPoolingGflags = new ArrayList<>();
    for (String flag : connectionPoolingGflags.keySet()) {
      if (!allowedGflagsForCurrentVersion.contains(flag) && !flag.startsWith("ysql_conn_mgr")) {
        invalidConnectionPoolingGflags.add(flag);
      }
    }
    if (!invalidConnectionPoolingGflags.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot set gflags '%s' as they are not related to Connection Pooling in version"
                  + " '%s'.",
              invalidConnectionPoolingGflags.toString(), ybdbVersion));
    }
    LOG.info(
        "Successfully validated that all the gflags '{}' are related to connection pooling for"
            + " version '{}'.",
        connectionPoolingGflags.keySet().toString(),
        ybdbVersion);
  }

  public synchronized void fetchGFlagFilesFromTarGZipInputStream(
      InputStream inputStream,
      String dbVersion,
      List<String> requiredGFlagFileList,
      String releasesPath)
      throws IOException {
    if (requiredGFlagFileList.isEmpty()) {
      return;
    }
    List<String> missingRequiredGFlagFileList =
        requiredGFlagFileList.stream()
            .filter(
                file ->
                    !(new File(String.format("%s/%s/%s", releasesPath, dbVersion, file))).exists())
            .collect(Collectors.toList());
    if (missingRequiredGFlagFileList.isEmpty()) {
      return;
    }
    LOG.info("Adding {} files for DB version {}", missingRequiredGFlagFileList, dbVersion);
    YsqlMigrationFilesList migrationFilesList = new YsqlMigrationFilesList();
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(new GzipCompressorInputStream(inputStream))) {
      TarArchiveEntry currentEntry;
      while ((currentEntry = tarInput.getNextEntry()) != null) {
        if (isYSQLMigrationFile(currentEntry.getName())) {
          String migrationFileName = getYsqlMigrationFiles(currentEntry.getName());
          migrationFilesList.ysqlMigrationsFilesList.add(migrationFileName);
          continue;
        }

        // Ignore all non-flag xml and auto flags files.
        if (!currentEntry.isFile() || !isFlagFile(currentEntry.getName())) {
          continue;
        }
        // Generally, we get the currentEntry variable value for the
        // gFlag file like `dbVersion/master_flags.xml`
        List<String> tarGFlagFilePathList = Arrays.asList(currentEntry.getName().split("/"));
        if (tarGFlagFilePathList.size() == 0) {
          continue;
        }
        String gFlagFileName = tarGFlagFilePathList.get(tarGFlagFilePathList.size() - 1);
        // Don't modify/re-write existing gFlags files, only add missing ones.
        if (!missingRequiredGFlagFileList.contains(gFlagFileName)) {
          continue;
        }
        String absoluteGFlagFileName =
            String.format("%s/%s/%s", releasesPath, dbVersion, gFlagFileName);
        File gFlagOutputFile = new File(absoluteGFlagFileName);
        if (!gFlagOutputFile.exists()) {
          gFlagOutputFile.getParentFile().mkdirs();
          gFlagOutputFile.createNewFile();
          BufferedInputStream in = new BufferedInputStream(tarInput);
          ByteArrayOutputStream out = new ByteArrayOutputStream();
          IOUtils.copy(in, out);
          try (OutputStream outputStream = new FileOutputStream(gFlagOutputFile)) {
            out.writeTo(outputStream);
          } catch (IOException e) {
            LOG.error(
                "Caught an error while adding {} for DB version{}: {}",
                gFlagFileName,
                dbVersion,
                e);
            throw e;
          }
        }
      }
      if (missingRequiredGFlagFileList.contains(YSQL_MIGRATION_FILES_LIST_FILE_NAME)) {
        File ysqlMigrationFileListFile =
            new File(
                String.format(
                    "%s/%s/%s", releasesPath, dbVersion, YSQL_MIGRATION_FILES_LIST_FILE_NAME));
        if (!Files.exists(Paths.get(ysqlMigrationFileListFile.getAbsolutePath()))) {
          ysqlMigrationFileListFile.getParentFile().mkdirs();
          ysqlMigrationFileListFile.createNewFile();
          ObjectMapper mapper = new ObjectMapper();
          FileUtils.writeJsonFile(
              ysqlMigrationFileListFile.getAbsolutePath(),
              (JsonNode) mapper.valueToTree(migrationFilesList));
        }
      }
    } catch (IOException e) {
      LOG.error("Caught an error while adding DB metadata for version: {}", dbVersion, e);
      throw e;
    }
  }

  public boolean checkGFlagFileExists(String releasesPath, String dbVersion, String gFlagFileName) {
    String filePath = String.format("%s/%s/%s", releasesPath, dbVersion, gFlagFileName);
    return Files.exists(Paths.get(filePath));
  }

  public AutoFlagsPerServer extractAutoFlags(String version, ServerType serverType)
      throws IOException {
    if (serverType.equals(ServerType.MASTER)) {
      return extractAutoFlags(version, "yb-master");
    } else if (serverType.equals(ServerType.TSERVER)) {
      return extractAutoFlags(version, "yb-tserver");
    }
    return null;
  }

  public void addDBMetadataFiles(String version) {
    List<String> missingFiles = getMissingFlagFiles(version);
    if (missingFiles.size() == 0) {
      return;
    }
    LOG.info("Adding {} files for version: {}", missingFiles, version);
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    try (InputStream tarGZIPInputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
      fetchGFlagFilesFromTarGZipInputStream(
          tarGZIPInputStream, version, missingFiles, releasesPath);
    } catch (Exception e) {
      LOG.error("Error in fetching GFlags metadata: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error in adding DB metadata files for DB version " + version);
    }
  }

  private List<String> getMissingFlagFiles(String version) {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    List<String> missingFiles = new ArrayList<>();
    for (String fileName : GFlagsValidation.GFLAG_FILENAME_LIST) {
      File file = new File(String.format("%s/%s/%s", releasesPath, version, fileName));
      if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
        missingFiles.add(fileName);
      }
    }
    if (missingFiles.contains(Util.AUTO_FLAG_FILENAME)
        && !CommonUtils.isAutoFlagSupported(version)) {
      missingFiles.remove(Util.AUTO_FLAG_FILENAME);
    }
    if (missingFiles.contains(YSQL_MIGRATION_FILES_LIST_FILE_NAME)
        && !CommonUtils.isReleaseEqualOrAfter(Util.YBDB_ROLLBACK_DB_VERSION, version)) {
      missingFiles.remove(YSQL_MIGRATION_FILES_LIST_FILE_NAME);
    }
    return missingFiles;
  }

  /**
   * Returns list of auto flags from auto_flags.json. This list contains all auto flags present in a
   * version.
   *
   * @param version
   * @param serverType
   * @return
   * @throws IOException
   */
  public AutoFlagsPerServer extractAutoFlags(String version, String serverType) throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    File autoFlagFile = Paths.get(releasesPath, version, Util.AUTO_FLAG_FILENAME).toFile();
    if (!Files.exists(Paths.get(autoFlagFile.getAbsolutePath()))) {
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream, version, Collections.singletonList(Util.AUTO_FLAG_FILENAME), releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting flags from DB package: ", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting flags from DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    try (InputStream inputStream = FileUtils.getInputStreamOrFail(autoFlagFile)) {
      AutoFlags data = objectMapper.readValue(inputStream, AutoFlags.class);
      AutoFlagsPerServer autoFlags =
          data.autoFlagsPerServers.stream()
              .filter(flags -> flags.serverType.equals(serverType))
              .findFirst()
              .get();
      if (autoFlags != null) {
        autoFlags.autoFlagDetails =
            autoFlags.autoFlagDetails.stream()
                .filter(flag -> !TEST_AUTO_FLAGS.contains(flag.name))
                .collect(Collectors.toList());
      }
      return autoFlags;
    }
  }

  public Map<String, String> getFilteredAutoFlagsWithNonInitialValue(
      Map<String, String> flags, String version, ServerType serverType) throws IOException {
    Map<String, String> filteredList = new HashMap<>();
    if (MapUtils.isEmpty(flags)) {
      return filteredList;
    }
    Map<String, GFlagDetails> allGFlagsMap =
        extractGFlags(version, serverType.name(), false).stream()
            .collect(Collectors.toMap(flagDetails -> flagDetails.name, Function.identity()));
    for (Map.Entry<String, String> entry : flags.entrySet()) {
      String flag = entry.getKey();
      if (!allGFlagsMap.containsKey(flag)) {
        throw new PlatformServiceException(BAD_REQUEST, flag + " is not present in metadata.");
      }
      GFlagDetails flagDetail = allGFlagsMap.get(flag);
      if (isAutoFlag(flagDetail) && !flagDetail.initial.equals(entry.getValue())) {
        filteredList.put(entry.getKey(), entry.getValue());
      }
    }
    return filteredList;
  }

  private Set<String> getFlagsTagSet(GFlagDetails flagDetails) {
    if (StringUtils.isEmpty(flagDetails.tags)) {
      return new HashSet<>();
    }
    return new HashSet<>(Arrays.asList(StringUtils.splitPreserveAllTokens(flagDetails.tags, ",")));
  }

  public boolean isAutoFlag(GFlagDetails flag) {
    return getFlagsTagSet(flag).contains("auto");
  }

  private boolean isFlagFile(String fileName) {
    return fileName.endsWith("flags.xml")
        || fileName.endsWith(Util.AUTO_FLAG_FILENAME)
        || fileName.endsWith(Util.DB_VERSION_METADATA_FILENAME);
  }

  private boolean isYSQLMigrationFile(String fileName) {
    return fileName.contains("ysql_migration") && fileName.endsWith(".sql");
  }

  private String getYsqlMigrationFiles(String fileFullPath) {
    List<String> migrationFileNameList = Arrays.asList(fileFullPath.split("/"));
    String migrationFileName = migrationFileNameList.get(migrationFileNameList.size() - 1);
    return migrationFileName.substring(migrationFileName.indexOf("__") + 2);
  }

  public Set<String> getYsqlMigrationFilesList(String version) throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    String filePath =
        String.format("%s/%s/%s", releasesPath, version, YSQL_MIGRATION_FILES_LIST_FILE_NAME);
    File file = new File(filePath);
    if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream,
            version,
            Collections.singletonList(YSQL_MIGRATION_FILES_LIST_FILE_NAME),
            releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting YSQL migration from DB package", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting YSQL migration form DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    try (InputStream inputStream = FileUtils.getInputStreamOrFail(file)) {
      YsqlMigrationFilesList data =
          objectMapper.readValue(inputStream, YsqlMigrationFilesList.class);
      return data.ysqlMigrationsFilesList;
    }
  }

  public Optional<Double> getGlibcVersion(String version) throws IOException {
    File file = getDBMetadataFile(version);
    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode jsonNode = objectMapper.readTree(file);
    if (jsonNode.has(GLIBC_VERSION_FIELD_NAME)) {
      String glibc = jsonNode.get(GLIBC_VERSION_FIELD_NAME).asText();
      return Optional.of(Double.parseDouble(glibc));
    } else {
      return Optional.empty();
    }
  }

  public Optional<Integer> getYsqlMajorVersion(String version) throws IOException {
    File file = getDBMetadataFile(version);
    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode jsonNode = objectMapper.readTree(file);
    if (jsonNode.has(YSQL_MAJOR_VERSION_FIELD_NAME)) {
      int ysqlMajorVersion = jsonNode.get(YSQL_MAJOR_VERSION_FIELD_NAME).asInt();
      return Optional.of(ysqlMajorVersion);
    } else {
      return Optional.empty();
    }
  }

  private File getDBMetadataFile(String version) {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    String filePath =
        String.format("%s/%s/%s", releasesPath, version, Util.DB_VERSION_METADATA_FILENAME);
    File file = new File(filePath);
    if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream,
            version,
            Collections.singletonList(Util.DB_VERSION_METADATA_FILENAME),
            releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting version metadata from DB package", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting version metadata form DB package");
      }
    }
    return file;
  }

  public boolean ysqlMajorVersionUpgrade(String oldVersion, String newVersion) {
    try {
      Optional<Integer> newVersionYsqlVersion = getYsqlMajorVersion(newVersion);
      Optional<Integer> oldVersionYsqlVersion = getYsqlMajorVersion(oldVersion);

      // We assume that old db version that does not contains ysql major version are on pg-11.
      if (!newVersionYsqlVersion.isPresent()) {
        return false;
      }
      if (newVersionYsqlVersion.get().equals(15)) {
        if (oldVersionYsqlVersion.isPresent()) {
          if (newVersionYsqlVersion.get().equals(oldVersionYsqlVersion.get())) {
            return false;
          } else {
            return true;
          }
        } else {
          return true;
        }
      }
      return false;
    } catch (Exception e) {
      LOG.error("failed to get ysql major version", e);
      throw new RuntimeException(e);
    }
  }

  /** Structure to capture GFlags metadata from xml file. */
  private static class AllGFlags {
    @JsonProperty(value = "program")
    public String program;

    @JsonProperty(value = "usage")
    public String Usage;

    @JacksonXmlElementWrapper(useWrapping = false)
    @JsonProperty(value = "flag")
    public List<GFlagDetails> flags;
  }

  /** Structure to capture most used gflags from json file. */
  private static class MostUsedGFlags {
    @JsonProperty(value = "MASTER")
    List<String> masterGFlags;

    @JsonProperty(value = "TSERVER")
    List<String> tserverGFlags;
  }

  /** Structure to capture Auto Flags details from json file */
  public static class AutoFlags {
    @JsonProperty(value = "auto_flags")
    public List<AutoFlagsPerServer> autoFlagsPerServers;
  }

  public static class AutoFlagsPerServer {
    @JsonAlias(value = "program")
    public String serverType;

    @JsonAlias(value = "flags")
    public List<AutoFlagDetails> autoFlagDetails;
  }

  @EqualsAndHashCode(callSuper = false)
  public static class AutoFlagDetails {
    @JsonAlias(value = "name")
    public String name;

    @JsonAlias(value = "class")
    public int flagClass;

    @JsonAlias(value = "is_runtime")
    public boolean runtime;
  }

  public static class YsqlMigrationFilesList {
    @JsonAlias(value = "ysqlMigrationsFilesList")
    public Set<String> ysqlMigrationsFilesList = new HashSet<>();
  }
}

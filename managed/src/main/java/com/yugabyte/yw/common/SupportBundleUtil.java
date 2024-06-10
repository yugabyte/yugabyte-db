/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.getDataDirectoryPath;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.KubernetesManager.RoleData;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.zip.GZIPInputStream;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.ListUtils;
import org.apache.commons.compress.archivers.ArchiveException;
import org.apache.commons.compress.archivers.ArchiveStreamFactory;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.exception.ExceptionUtils;
import org.apache.commons.lang3.time.DateUtils;
import org.joda.time.DateTime;
import play.libs.Json;

@Slf4j
@Singleton
public class SupportBundleUtil {

  public static final String kubectlOutputFormat = "yaml";

  public Date getDateNDaysAgo(Date currDate, int days) {
    Date dateNDaysAgo = new DateTime(currDate).minusDays(days).toDate();
    return dateNDaysAgo;
  }

  public Date getDateNDaysAfter(Date currDate, int days) {
    Date dateNDaysAgo = new DateTime(currDate).plusDays(days).toDate();
    return dateNDaysAgo;
  }

  public Date getTodaysDate() throws ParseException {
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
    Date dateToday = sdf.parse(sdf.format(new Date()));
    return dateToday;
  }

  public Date getDateFromBundleFileName(String fileName) throws ParseException {
    SimpleDateFormat bundleSdf = new SimpleDateFormat("yyyyMMddHHmmss.SSS");
    SimpleDateFormat newSdf = new SimpleDateFormat("yyyy-MM-dd");

    String[] fileNameSplit = fileName.split("-");
    String fileDateStr = fileNameSplit[fileNameSplit.length - 2];
    return newSdf.parse(newSdf.format(bundleSdf.parse(fileDateStr)));
  }

  public boolean isValidDate(Date date) {
    return date != null;
  }

  // Checks if a given date is between 2 other given dates (startDate and endDate both inclusive)
  public boolean checkDateBetweenDates(Date dateToCheck, Date startDate, Date endDate) {
    return !dateToCheck.before(startDate) && !dateToCheck.after(endDate);
  }

  public List<String> sortDatesWithPattern(List<String> datesList, String sdfPattern) {
    // Sort the list of dates based on the given 'SimpleDateFormat' pattern
    List<String> sortedList = new ArrayList<String>(datesList);
    Collections.sort(
        sortedList,
        new Comparator<String>() {
          DateFormat f = new SimpleDateFormat(sdfPattern);

          @Override
          public int compare(String o1, String o2) {
            try {
              return f.parse(o1).compareTo(f.parse(o2));
            } catch (ParseException e) {
              return 0;
            }
          }
        });

    return sortedList;
  }

  /**
   * Filter and return only the strings which match given regex patterns.
   *
   * @param list original list of paths
   * @param regexList list of regex strings to match against any of them
   * @return list of paths after regex filtering
   */
  public List<Path> filterList(List<Path> list, List<String> regexList) {
    List<Path> result = new ArrayList<Path>();
    for (Path entry : list) {
      for (String regex : regexList) {
        if (entry.toString().matches(regex)) {
          result.add(entry);
        }
      }
    }
    return result;
  }

  // Gets the path to "yb-data/" folder on the node (Ex: "/mnt/d0", "/mnt/disk0")
  public String getDataDirPath(
      Universe universe, NodeDetails node, NodeUniverseManager nodeUniverseManager, Config config) {

    return getDataDirectoryPath(universe, node, config);
  }

  public void deleteFile(Path filePath) {
    if (FileUtils.deleteQuietly(new File(filePath.toString()))) {
      log.info("Successfully deleted file with path: {}", filePath);
    } else {
      log.info("Failed to delete file with path: {}", filePath);
    }
  }

  public void deleteSupportBundle(SupportBundle supportBundle) {
    // Delete the actual archive file
    Path supportBundlePath = supportBundle.getPathObject();
    if (supportBundlePath != null) {
      deleteFile(supportBundlePath);
    }
    // Deletes row from the support_bundle db table
    SupportBundle.delete(supportBundle.getBundleUUID());
  }

  /**
   * Uses capturing groups in regex pattern for easy retrieval of the file type. File type is
   * considered to be the first capturing group in the file name regex. Used to segregate files
   * based on master, tserver, WARNING, INFO, postgresql, controller, etc.
   *
   * <p>Example: If file name =
   * "/mnt/disk0/yb-data/yb-data/tserver/logs/postgresql-2022-11-15_000000.log", Then file type =
   * "/mnt/disk0/yb-data/yb-data/tserver/logs/postgresql-"
   *
   * @param fileName the entire file name or path
   * @param fileRegexList list of regex strings to match against any of them
   * @return the file type string
   */
  public String extractFileTypeFromFileNameAndRegex(String fileName, List<String> fileRegexList) {
    String fileType = "";
    try {
      for (String fileRegex : fileRegexList) {
        Matcher fileNameMatcher = Pattern.compile(fileRegex).matcher(fileName);
        if (fileNameMatcher.matches()) {
          fileType = fileNameMatcher.group(1);
          return fileType;
        }
      }
    } catch (Exception e) {
      log.error(
          "Could not extract file type from file name '{}' and regex list '{}'.",
          fileName,
          fileRegexList);
    }
    return fileType;
  }

  /**
   * Uses capturing groups in regex pattern for easier retrieval of neccessary info. Extracts dates
   * in formats "yyyyMMdd" and "yyyy-MM-dd" in a captured group in the file regex.
   *
   * @param fileName the entire file name or path
   * @param fileRegexList list of regex strings to match against any of them
   * @return the date in the file name regex group
   */
  public Date extractDateFromFileNameAndRegex(String fileName, List<String> fileRegexList) {
    Date fileDate = new Date(0);
    try {
      for (String fileRegex : fileRegexList) {
        Matcher fileNameMatcher = Pattern.compile(fileRegex).matcher(fileName);
        if (fileNameMatcher.matches()) {
          for (int groupIndex = 1; groupIndex <= fileNameMatcher.groupCount(); ++groupIndex) {
            try {
              String fileDateString = fileNameMatcher.group(groupIndex);
              // yyyyMMdd -> for master, tserver, controller log file names
              // yyyy-MM-dd -> for postgres log file names
              String[] possibleDateFormats = {"yyyyMMdd", "yyyy-MM-dd"};
              fileDate = DateUtils.parseDate(fileDateString, possibleDateFormats);
              return fileDate;
            } catch (Exception e) {
              // Do nothing and skip
              // We don't want to log this because it pollutes the logs.
            }
          }
        }
      }
    } catch (Exception e) {
      log.error(
          "Could not extract date from file name '{}' and regex list '{}'.",
          fileName,
          fileRegexList);
    }
    return fileDate;
  }

  /**
   * Filters a list of log file paths with regex pattern/s and between given start and end dates.
   *
   * <p>Core logic for a loose bound filtering based on dates (little bit tricky): Gets all the
   * files which have logs for requested time period, even when partial log statements present in
   * the file before the start date. Example: Assume log files are as follows (d1 = day 1, d2 = day
   * 2, ... in sorted order) => d1.gz, d2.gz, d5.gz => And user requested {startDate = d3, endDate =
   * d6} => Output files will be: {d2.gz, d5.gz} Due to d2.gz having all the logs from d2-d4,
   * therefore overlapping with given startDate
   *
   * @param logFilePaths list of file paths to filter and retrieve.
   * @param fileRegexList list of regex strings to match against any of them.
   * @param startDate the start date to filter from (inclusive).
   * @param endDate the end date to filter till (inclusive).
   * @return list of paths after filtering based on dates.
   * @throws ParseException
   */
  public List<Path> filterFilePathsBetweenDates(
      List<Path> logFilePaths, List<String> fileRegexList, Date startDate, Date endDate)
      throws ParseException {

    // Final filtered log paths
    List<Path> filteredLogFilePaths = new ArrayList<>();

    // Initial filtering of the file names based on regex
    logFilePaths = filterList(logFilePaths, fileRegexList);

    // Map of the <fileType, List<filePath>>
    // This is required so that we can filter each type of file according to start and end dates.
    // Example of map:
    // {"/mnt/d0/master/logs/log.INFO." :
    //    ["/mnt/d0/master/logs/log.INFO.20221120-000000.log.gz",
    //     "/mnt/d0/master/logs/log.INFO.20221121-000000.log"]}
    // The reason we don't use a map of <fileType, List<Date>> is because we need to return the
    // entire path.
    Map<String, List<Path>> fileTypeToDate =
        logFilePaths.stream()
            .collect(
                Collectors.groupingBy(
                    p -> extractFileTypeFromFileNameAndRegex(p.toString(), fileRegexList)));

    // Loop through each file type
    for (String fileType : fileTypeToDate.keySet()) {
      // Sort the files in descending order of extracted date
      Collections.sort(
          fileTypeToDate.get(fileType),
          new Comparator<Path>() {
            @Override
            public int compare(Path path1, Path path2) {
              Date date1 = extractDateFromFileNameAndRegex(path1.toString(), fileRegexList);
              Date date2 = extractDateFromFileNameAndRegex(path2.toString(), fileRegexList);
              return date2.compareTo(date1);
            }
          });

      // Filter file paths according to start and end dates
      // Add filtered date paths to final list
      Date extraStartDate = null;
      for (Path filePathToCheck : fileTypeToDate.get(fileType)) {
        Date dateToCheck =
            extractDateFromFileNameAndRegex(filePathToCheck.toString(), fileRegexList);
        if (checkDateBetweenDates(dateToCheck, startDate, endDate)) {
          filteredLogFilePaths.add(filePathToCheck);
        }
        // This is required to collect extra log/s before the start date for partial overlap
        if ((extraStartDate == null && dateToCheck.before(startDate))
            || (extraStartDate != null && extraStartDate.equals(dateToCheck))) {
          extraStartDate = dateToCheck;
          filteredLogFilePaths.add(filePathToCheck);
        }
      }
    }

    return filteredLogFilePaths;
  }

  /**
   * Ensures that all directories exist along the given path by creating them if absent.
   *
   * @param dirPath the path to create directories.
   * @return the Path object of the original path.
   * @throws IOException if not able to create / access the files properly.
   */
  public Path createDirectories(String dirPath) throws IOException {
    return Files.createDirectories(Paths.get(dirPath));
  }

  /**
   * Logs error encountered while getting any k8s support bundle file to the local target file
   * location
   *
   * @param errorMessage Error message to be written to the file
   * @param e Exception which caused the error
   * @param localFilePath target file to which the error has to be written
   */
  public void logK8sError(String errorMessage, Exception e, String localFilePath) {
    log.error(errorMessage, e);

    String fileErrorMessage =
        errorMessage + System.lineSeparator() + ExceptionUtils.getStackTrace(e);
    writeStringToFile(fileErrorMessage, localFilePath);
  }

  public enum KubernetesResourceType {
    PODS,
    CONFIGMAPS,
    SERVICES,
    STATEFULSETS,
    PERSISTENTVOLUMECLAIMS,
    SECRETS,
    EVENTS,
    STORAGECLASS
  }

  @Data
  @ToString(includeFieldNames = true)
  @AllArgsConstructor
  public static class KubernetesCluster {
    public String clusterName;
    public Map<String, String> config;
    public Map<String, String> namespaceToAzNameMap;

    /**
     * Checks if the list of KubernetesCluster objects contains a cluster with a given name.
     *
     * @param kubernetesClusters the list of k8s clusters objects.
     * @param clusterName the cluster name to check for.
     * @return true if it already exists, else false.
     */
    public static boolean listContainsClusterName(
        List<KubernetesCluster> kubernetesClusters, String clusterName) {
      return kubernetesClusters.stream()
          .map(KubernetesCluster::getClusterName)
          .filter(clusterName::equals)
          .findFirst()
          .isPresent();
    }

    /**
     * Returns the Kubernetes cluster object with a given name.
     *
     * @param kubernetesClusters the list of k8s clusters objects.
     * @param clusterName the cluster name to check for.
     * @return the kubernetes cluster object if it exists in the list, else null;
     * @throws Exception when multiple kubernetes clusters exist with the same name.
     */
    public static KubernetesCluster findKubernetesClusterWithName(
        List<KubernetesCluster> kubernetesClusters, String clusterName) throws Exception {
      List<KubernetesCluster> filteredKubernetesClusters =
          kubernetesClusters.stream()
              .filter(kubernetesCluster -> clusterName.equals(kubernetesCluster.getClusterName()))
              .collect(Collectors.toList());
      if (filteredKubernetesClusters == null || filteredKubernetesClusters.size() > 1) {
        throw new Exception("Found multiple kubernetes clusters with same cluster name.");
      }
      if (filteredKubernetesClusters.size() < 1) {
        return null;
      }
      return filteredKubernetesClusters.get(0);
    }

    /**
     * Adds a {namespace : azname} to a kubernetes cluster with the given name in a list of cluster
     * objects.
     *
     * @param kubernetesClusters the list of k8s clusters objects.
     * @param clusterName the cluster name to check for.
     * @param namespace the namespace in the kubernetes cluster to add.
     * @param azName the zone name corresponding to the given namespace to add.
     */
    public static void addNamespaceToKubernetesClusterInList(
        List<KubernetesCluster> kubernetesClusters,
        String clusterName,
        String namespace,
        String azName) {
      for (int i = 0; i < kubernetesClusters.size(); ++i) {
        if (kubernetesClusters.get(i).getClusterName().equals(clusterName)) {
          kubernetesClusters.get(i).namespaceToAzNameMap.put(namespace, azName);
        }
      }
    }
  }

  public boolean writeStringToFile(String message, String localFilePath) {
    try {
      FileUtils.writeStringToFile(new File(localFilePath), message, Charset.forName("UTF-8"));
      return true;
    } catch (IOException e) {
      log.error("Failed writing output string to file: ", e);
      return false;
    }
  }

  /**
   * Gets the kubernetes service account name from the provider config object. This is a best effort
   * to parse the service account from the kubeconfig user.
   *
   * @param provider the provider object for the universe cluster.
   * @param kubernetesManager the k8s manager object (Shell / Native).
   * @param config tell the k8s manager where kubeconfig is.
   * @return the service account name.
   */
  public String getServiceAccountName(
      Provider provider, KubernetesManager kubernetesManager, Map<String, String> config) {
    String serviceAccountName = "";
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);
    // If the provider has the KUBECONFIG_SERVICE_ACCOUNT key, we can use it directly. Otherwise,
    // we will attempt to parse the service account from the kubeconfig. Kubeconfigs generated using
    // generate_kubeconfig.py will have a user with the format <service account>-<cluster>.
    if (providerConfig.containsKey("KUBECONFIG_SERVICE_ACCOUNT")) {
      serviceAccountName = providerConfig.get("KUBECONFIG_SERVICE_ACCOUNT");
    } else {
      String username = kubernetesManager.getKubeconfigUser(config);
      String clusterName = kubernetesManager.getKubeconfigCluster(config);

      // Use regex to get the service account from the pattern (service account name)-(clusterName)
      Pattern pattern = Pattern.compile(String.format("^(.*)-%s", clusterName));
      Matcher matcher = pattern.matcher(username);
      if (matcher.find()) {
        serviceAccountName = matcher.group(1);
      }
    }

    return serviceAccountName;
  }

  /**
   * Gets the permissions for all the roles associated with the service account name and saves all
   * the outputs to a directory.
   *
   * @param kubernetesManager the k8s manager object (Shell / Native).
   * @param serviceAccountName the service account name to get permissions for.
   * @param destDir the local directory path to save the commands outputs to.
   */
  public void getServiceAccountPermissionsToFile(
      KubernetesManager kubernetesManager,
      Map<String, String> config,
      String serviceAccountName,
      String destDir,
      UUID universeUUID,
      String universeName) {
    List<RoleData> roleDataList =
        kubernetesManager.getAllRoleDataForServiceAccountName(config, serviceAccountName);
    log.debug(
        String.format(
            "Role data list for service account name '%s' = %s.",
            serviceAccountName, roleDataList.toString()));
    for (RoleData roleData : roleDataList) {
      String localFilePath =
          destDir
              + String.format(
                  "/get_%s_%s_%s.%s",
                  roleData.kind, roleData.name, roleData.namespace, kubectlOutputFormat);
      try {

        String resourceOutput =
            kubernetesManager.getServiceAccountPermissions(config, roleData, kubectlOutputFormat);
        writeStringToFile(resourceOutput, localFilePath);
      } catch (Exception e) {
        logK8sError(
            String.format(
                "Error when getting service account permissions for "
                    + "service account '%s' on universe (%s, %s) : ",
                serviceAccountName, universeUUID.toString(), universeName),
            e,
            localFilePath);
      }
    }
  }

  /**
   * Gets the set of all storage class names from all namespaces with master and tserver for a
   * particular kubernetes cluster.
   *
   * @param kubernetesManager the k8s manager object (Shell / Native).
   * @param kubernetesCluster the k8s cluster object.
   * @param isMultiAz if the provider is multi az.
   * @param nodePrefix the node prefix of the universe.
   * @param isReadOnlyUniverseCluster if the universe cluster is a read replica.
   * @return a set of all storage class names
   */
  public Set<String> getAllStorageClassNames(
      String universeName,
      KubernetesManager kubernetesManager,
      KubernetesCluster kubernetesCluster,
      boolean isMultiAz,
      String nodePrefix,
      boolean isReadOnlyUniverseCluster,
      boolean newNamingStyle) {
    Set<String> allStorageClassNames = new HashSet<String>();

    for (Map.Entry<String, String> namespaceToAzName :
        kubernetesCluster.namespaceToAzNameMap.entrySet()) {
      String namespace = namespaceToAzName.getKey();
      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              isMultiAz,
              nodePrefix,
              universeName,
              namespaceToAzName.getValue(),
              isReadOnlyUniverseCluster,
              newNamingStyle);

      String masterStorageClassName =
          kubernetesManager.getStorageClassName(
              kubernetesCluster.config, namespace, helmReleaseName, true, newNamingStyle);
      allStorageClassNames.add(masterStorageClassName);

      String tserverStorageClassName =
          kubernetesManager.getStorageClassName(
              kubernetesCluster.config, namespace, helmReleaseName, false, newNamingStyle);
      allStorageClassNames.add(tserverStorageClassName);
    }

    return allStorageClassNames;
  }

  /**
   * Untar an input file into an output file.
   *
   * <p>The output file is created in the output folder, having the same name as the input file,
   * minus the '.tar' extension.
   *
   * @param inputFile the input .tar file
   * @param outputDir the output directory file.
   * @throws IOException
   * @throws FileNotFoundException
   * @return The {@link List} of {@link File}s with the untared content.
   * @throws ArchiveException
   */
  public List<File> unTar(final File inputFile, final File outputDir)
      throws FileNotFoundException, IOException, ArchiveException {

    log.info(
        String.format(
            "Untaring %s to dir %s.", inputFile.getAbsolutePath(), outputDir.getAbsolutePath()));

    final List<File> untaredFiles = new LinkedList<File>();
    try (InputStream is = new FileInputStream(inputFile);
        TarArchiveInputStream debInputStream =
            (TarArchiveInputStream)
                new ArchiveStreamFactory().createArchiveInputStream("tar", is)) {
      TarArchiveEntry entry = null;
      while ((entry = (TarArchiveEntry) debInputStream.getNextEntry()) != null) {
        final File outputFile = new File(outputDir, entry.getName());
        if (entry.isDirectory()) {
          log.info(
              String.format(
                  "Attempting to write output directory %s.", outputFile.getAbsolutePath()));
          if (!outputFile.exists()) {
            log.info(
                String.format(
                    "Attempting to create output directory %s.", outputFile.getAbsolutePath()));
            if (!outputFile.mkdirs()) {
              throw new IllegalStateException(
                  String.format("Couldn't create directory %s.", outputFile.getAbsolutePath()));
            }
          }
        } else {
          // Don't log the output file here because platform logs get polluted.
          File parent = outputFile.getParentFile();
          if (!parent.exists()) parent.mkdirs();
          try (OutputStream outputFileStream = new FileOutputStream(outputFile)) {
            IOUtils.copy(debInputStream, outputFileStream);
          }
        }
        untaredFiles.add(outputFile);
      }
    }

    return untaredFiles;
  }

  /**
   * Ungzip an input file into an output file.
   *
   * <p>The output file is created in the output folder, having the same name as the input file,
   * minus the '.gz' extension.
   *
   * @param inputFile the input .gz file
   * @param outputDir the output directory file.
   * @throws IOException
   * @throws FileNotFoundException
   * @return The {@File} with the ungzipped content.
   */
  public File unGzip(final File inputFile, final File outputDir)
      throws FileNotFoundException, IOException {

    log.info(
        String.format(
            "Ungzipping %s to dir %s.", inputFile.getAbsolutePath(), outputDir.getAbsolutePath()));

    final File outputFile =
        new File(outputDir, inputFile.getName().substring(0, inputFile.getName().length() - 3));

    try (GZIPInputStream in = new GZIPInputStream(new FileInputStream(inputFile));
        FileOutputStream out = new FileOutputStream(outputFile)) {
      IOUtils.copy(in, out);
      return outputFile;
    }
  }

  public void batchWiseDownload(
      UniverseInfoHandler universeInfoHandler,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      Path nodeTargetFile,
      String nodeHomeDir,
      List<String> sourceNodeFiles,
      String componentName,
      boolean skipUntar) {
    // Run command for large number of files in batches.
    List<List<String>> batchesNodeFiles = ListUtils.partition(sourceNodeFiles, 1000);
    int batchIndex = 0;
    for (List<String> batchNodeFiles : batchesNodeFiles) {
      batchIndex++;
      log.debug("Running batch {} for {}.", batchIndex, componentName);
      Path targetFile =
          universeInfoHandler.downloadNodeFile(
              customer, universe, node, nodeHomeDir, batchNodeFiles, nodeTargetFile);
      try {
        if (Files.exists(targetFile)) {
          if (!skipUntar) {
            File unZippedFile =
                unGzip(
                    new File(targetFile.toAbsolutePath().toString()),
                    new File(bundlePath.toAbsolutePath().toString()));
            Files.delete(targetFile);
            unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
            unZippedFile.delete();
          }
        } else {
          log.debug(
              "No files exist at the source path '{}' for universe '{}' for component '{}'.",
              nodeHomeDir,
              universe.getName(),
              componentName);
        }
      } catch (Exception e) {
        log.error(
            String.format(
                "Something went wrong while trying to untar the files from "
                    + "component '%s' in the DB node: ",
                componentName),
            e);
      }
      log.debug("Finished running batch {} for {}.", batchIndex, componentName);
    }
  }

  public void downloadNodeLevelComponent(
      UniverseInfoHandler universeInfoHandler,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      String nodeHomeDir,
      List<String> sourceNodeFiles,
      String componentName,
      boolean skipUntar)
      throws Exception {
    if (node == null) {
      String errMsg =
          String.format(
              "Wrongly called downloadNodeLevelComponent() "
                  + "from '%s' with node = null, on universe = '%s'.",
              componentName, universe.getName());
      throw new RuntimeException(errMsg);
    }

    // Get target file path
    String nodeName = node.getNodeName();
    Path nodeTargetFile = Paths.get(bundlePath.toString(), componentName + ".tar.gz");

    log.debug(
        "Gathering '{}' for node: '{}', source path: '{}', target path: '{}'.",
        componentName,
        nodeName,
        nodeHomeDir,
        nodeTargetFile);

    // Download all logs batch wise
    batchWiseDownload(
        universeInfoHandler,
        customer,
        universe,
        bundlePath,
        node,
        nodeTargetFile,
        nodeHomeDir,
        sourceNodeFiles,
        componentName,
        skipUntar);
  }

  public void ignoreExceptions(Runnable r) {
    try {
      r.run();
    } catch (Exception e) {
      log.error("Error while trying to collect YBA Metadata subcomponent: ", e);
    }
  }

  public void saveMetadata(Customer customer, String destDir, JsonNode jsonData, String fileName) {
    // Declare file path.
    String metadataFilePath = Paths.get(destDir, fileName).toString();

    // Save the collected metadata to above file.
    writeStringToFile(jsonData.toPrettyString(), metadataFilePath);
    log.info(
        "Gathered '{}' data for customer '{}', at path '{}'.",
        fileName,
        customer.getUuid(),
        metadataFilePath);
  }

  public void getCustomerMetadata(Customer customer, String destDir) {
    // Gather metadata.
    JsonNode jsonData =
        RedactingService.filterSecretFields(Json.toJson(customer), RedactionTarget.APIS);

    // Save the above collected metadata.
    saveMetadata(customer, destDir, jsonData, "customer.json");
  }

  public void getUniversesMetadata(Customer customer, String destDir) {
    // Gather metadata.
    List<UniverseResp> universes =
        customer.getUniverses().stream().map(u -> new UniverseResp(u)).collect(Collectors.toList());
    JsonNode jsonData =
        RedactingService.filterSecretFields(Json.toJson(universes), RedactionTarget.APIS);

    // Save the above collected metadata.
    saveMetadata(customer, destDir, jsonData, "universes.json");
  }

  public void getProvidersMetadata(Customer customer, String destDir) {
    // Gather metadata.
    List<Provider> providers = Provider.getAll(customer.getUuid());
    providers.forEach(CloudInfoInterface::mayBeMassageResponse);
    JsonNode jsonData =
        RedactingService.filterSecretFields(Json.toJson(providers), RedactionTarget.APIS);

    // Save the above collected metadata.
    saveMetadata(customer, destDir, jsonData, "providers.json");
  }

  public void getUsersMetadata(Customer customer, String destDir) {
    // Gather metadata.
    List<Users> users = Users.getAll(customer.getUuid());
    JsonNode jsonData =
        RedactingService.filterSecretFields(Json.toJson(users), RedactionTarget.APIS);

    // Save the above collected metadata.
    saveMetadata(customer, destDir, jsonData, "users.json");
  }

  public void getTaskMetadata(Customer customer, String destDir) {
    // Gather metadata for customer_task table.
    List<CustomerTask> customerTasks = CustomerTask.getByCustomerUUID(customer.getUuid());
    // Save the above collected metadata.
    JsonNode customerTaskJsonData =
        RedactingService.filterSecretFields(Json.toJson(customerTasks), RedactionTarget.APIS);
    saveMetadata(customer, destDir, customerTaskJsonData, "customer_task.json");

    // Gather metadata for task_info table later.
  }

  public void getInstanceTypeMetadata(Customer customer, String destDir) {
    // Gather metadata.
    Set<UUID> providerUUIDs =
        Provider.getAll(customer.getUuid()).stream()
            .map(Provider::getUuid)
            .collect(Collectors.toSet());
    List<InstanceType> instanceTypes =
        InstanceType.getAllInstanceTypes().stream()
            .filter(it -> providerUUIDs.contains(it.getProvider().getUuid()))
            .collect(Collectors.toList());
    JsonNode jsonData =
        RedactingService.filterSecretFields(Json.toJson(instanceTypes), RedactionTarget.APIS);

    // Save the above collected metadata.
    saveMetadata(customer, destDir, jsonData, "instance_type.json");
  }

  public void gatherAndSaveAllMetadata(Customer customer, String destDir) {
    ignoreExceptions(() -> getCustomerMetadata(customer, destDir));
    ignoreExceptions(() -> getUniversesMetadata(customer, destDir));
    ignoreExceptions(() -> getProvidersMetadata(customer, destDir));
    ignoreExceptions(() -> getUsersMetadata(customer, destDir));
    ignoreExceptions(() -> getTaskMetadata(customer, destDir));
    ignoreExceptions(() -> getInstanceTypeMetadata(customer, destDir));
  }
}

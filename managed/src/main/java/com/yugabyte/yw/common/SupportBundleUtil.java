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

import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.KubernetesManager.RoleData;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
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
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.ToString;
import java.util.zip.GZIPInputStream;

import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.compress.archivers.ArchiveException;
import org.apache.commons.compress.archivers.ArchiveStreamFactory;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.ObjectUtils;
import org.apache.commons.lang3.time.DateUtils;
import org.joda.time.DateTime;

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

  public List<String> filterList(List<String> list, String regex) {
    // Filter and return only the strings which match a given regex pattern
    List<String> result = new ArrayList<String>();
    for (String entry : list) {
      if (entry.matches(regex)) {
        result.add(entry);
      }
    }
    return result;
  }

  // Gets the path to "yb-data/" folder on the node (Ex: "/mnt/d0", "/mnt/disk0")
  public String getDataDirPath(
      Universe universe, NodeDetails node, NodeUniverseManager nodeUniverseManager, Config config) {
    String dataDirPath = config.getString("yb.support_bundle.default_mount_point_prefix") + "0";
    UserIntent userIntent = universe.getCluster(node.placementUuid).userIntent;
    CloudType cloudType = userIntent.providerType;

    if (cloudType == CloudType.onprem) {
      // On prem universes:
      // Onprem universes have to specify the mount points for the volumes at the time of provider
      // creation itself.
      // This is stored at universe.cluster.userIntent.deviceInfo.mountPoints
      try {
        String mountPoints = userIntent.deviceInfo.mountPoints;
        dataDirPath = mountPoints.split(",")[0];
      } catch (Exception e) {
        log.error(String.format("On prem invalid mount points. Defaulting to %s", dataDirPath), e);
      }
    } else if (cloudType == CloudType.kubernetes) {
      // Kubernetes universes:
      // K8s universes have a default mount path "/mnt/diskX" with X = {0, 1, 2...} based on number
      // of volumes
      // This is specified in the charts repo:
      // https://github.com/yugabyte/charts/blob/master/stable/yugabyte/templates/service.yaml
      String mountPoint = config.getString("yb.support_bundle.k8s_mount_point_prefix");
      dataDirPath = mountPoint + "0";
    } else {
      // Other provider based universes:
      // Providers like GCP, AWS have the mountPath stored in the instance types for the most part.
      // Some instance types don't have mountPath initialized. In such cases, we default to
      // "/mnt/d0"
      try {
        String nodeInstanceType = node.cloudInfo.instance_type;
        String providerUUID = userIntent.provider;
        InstanceType instanceType =
            InstanceType.getOrBadRequest(UUID.fromString(providerUUID), nodeInstanceType);
        List<VolumeDetails> volumeDetailsList = instanceType.instanceTypeDetails.volumeDetailsList;
        if (CollectionUtils.isNotEmpty(volumeDetailsList)) {
          dataDirPath = volumeDetailsList.get(0).mountPath;
        } else {
          log.info(String.format("Mount point is not defined. Defaulting to %s", dataDirPath));
        }
      } catch (Exception e) {
        log.error(String.format("Could not get mount points. Defaulting to %s", dataDirPath), e);
      }
    }
    return dataDirPath;
  }

  public void deleteFile(Path filePath) {
    if (FileUtils.deleteQuietly(new File(filePath.toString()))) {
      log.info("Successfully deleted file with path: " + filePath.toString());
    } else {
      log.info("Failed to delete file with path: " + filePath.toString());
    }
  }

  // Filters a list of log file paths with a regex pattern and between given start and end dates
  public List<String> filterFilePathsBetweenDates(
      List<String> logFilePaths,
      String ybcLogsRegexPattern,
      Date startDate,
      Date endDate,
      boolean isYbc)
      throws ParseException {
    // Filtering the file names based on regex
    logFilePaths = filterList(logFilePaths, ybcLogsRegexPattern);

    // Sort the files in descending order of date (done implicitly as date format is yyyyMMdd)
    Collections.sort(logFilePaths, Collections.reverseOrder());

    // Core logic for a loose bound filtering based on dates (little bit tricky):
    // Gets all the files which have logs for requested time period,
    // even when partial log statements present in the file.
    // ----------------------------------------
    // Ex: Assume log files are as follows (d1 = day 1, d2 = day 2, ... in sorted order)
    // => d1.gz, d2.gz, d5.gz
    // => And user requested {startDate = d3, endDate = d6}
    // ----------------------------------------
    // => Output files will be: {d2.gz, d5.gz}
    // Due to d2.gz having all the logs from d2-d4, therefore overlapping with given startDate
    Date minDate = null;
    List<String> filteredLogFilePaths = new ArrayList<>();
    for (String filePath : logFilePaths) {
      String fileName =
          filePath.substring(filePath.lastIndexOf('/') + 1, filePath.lastIndexOf('-'));
      String trimmedFilePath = null;
      if (isYbc) {
        // Need trimmed file path starting from {./controller} for above function
        trimmedFilePath = filePath.split("ybc-data/")[1];
      } else {
        // Need trimmed file path starting from {./master, ./tserver} for above function
        trimmedFilePath = filePath.split("yb-data/")[1];
      }
      Matcher fileNameMatcher = Pattern.compile(ybcLogsRegexPattern).matcher(filePath);
      if (fileNameMatcher.matches()) {
        // Uses capturing and non capturing groups in regex pattern for easier retrieval of
        // neccessary info.
        // Group 1 = "yyyyMMdd" format in the file name of normal tserver logs.
        // Group 2 = "yyyy-MM-dd" format for postgres log file names.
        // dateStringsInFileName is a list of all the captured groups in the file name.
        // It is useful for capturing both tserver file dates and postgres file dates
        List<String> dateStringsInFileName = new ArrayList<>();
        for (int groupIndex = 1; groupIndex <= fileNameMatcher.groupCount(); ++groupIndex) {
          dateStringsInFileName.add(fileNameMatcher.group(groupIndex));
        }
        // Only one of the groups captured in the file name has a non null date
        String fileDateString =
            ObjectUtils.firstNonNull(
                dateStringsInFileName.toArray(new String[dateStringsInFileName.size()]));
        // yyyyMMdd -> for master and tserver log file names
        // yyyy-MM-dd -> for postgres log file names
        String[] possibleDateFormats = {"yyyyMMdd", "yyyy-MM-dd"};
        Date fileDate = DateUtils.parseDate(fileDateString, possibleDateFormats);
        if (checkDateBetweenDates(fileDate, startDate, endDate)) {
          filteredLogFilePaths.add(trimmedFilePath);
        } else if ((minDate == null && fileDate.before(startDate))
            || (minDate != null && fileDate.equals(minDate))) {
          filteredLogFilePaths.add(trimmedFilePath);
          minDate = fileDate;
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
      return kubernetesClusters
          .stream()
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
          kubernetesClusters
              .stream()
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
   * Gets the kubernetes service account name from the provider config object.
   *
   * @param provider the provider object for the universe cluster.
   * @return the service account name.
   */
  public String getServiceAccountName(Provider provider) {
    String serviceAccountName = "";
    Map<String, String> providerConfig = provider.getUnmaskedConfig();
    if (providerConfig.containsKey("KUBECONFIG_SERVICE_ACCOUNT")) {
      serviceAccountName = providerConfig.get("KUBECONFIG_SERVICE_ACCOUNT");
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
      String destDir) {
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
      String resourceOutput =
          kubernetesManager.getServiceAccountPermissions(config, roleData, kubectlOutputFormat);
      writeStringToFile(resourceOutput, localFilePath);
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
    final InputStream is = new FileInputStream(inputFile);
    final TarArchiveInputStream debInputStream =
        (TarArchiveInputStream) new ArchiveStreamFactory().createArchiveInputStream("tar", is);
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
        log.info(String.format("Creating output file %s.", outputFile.getAbsolutePath()));
        File parent = outputFile.getParentFile();
        if (!parent.exists()) parent.mkdirs();
        final OutputStream outputFileStream = new FileOutputStream(outputFile);
        IOUtils.copy(debInputStream, outputFileStream);
        outputFileStream.close();
      }
      untaredFiles.add(outputFile);
    }
    debInputStream.close();

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

    final GZIPInputStream in = new GZIPInputStream(new FileInputStream(inputFile));
    final FileOutputStream out = new FileOutputStream(outputFile);

    IOUtils.copy(in, out);

    in.close();
    out.close();

    return outputFile;
  }

  public void downloadNodeLevelComponent(
      UniverseInfoHandler universeInfoHandler,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      String nodeHomeDir,
      String sourceNodeFiles,
      String componentName)
      throws Exception {
    if (node == null) {
      String errMsg =
          String.format(
              "Wrongly called downloadNodeLevelComponent() "
                  + "from '%s' with node = null, on universe = '%s'.",
              componentName, universe.name);
      throw new RuntimeException(errMsg);
    }

    // Get target file path
    String nodeName = node.getNodeName();
    Path nodeTargetFile = Paths.get(bundlePath.toString(), componentName + ".tar.gz");

    log.debug(
        String.format(
            "Gathering '%s' for node: '%s', source path: '%s', target path: '%s'.",
            componentName, nodeName, nodeHomeDir, nodeTargetFile.toString()));

    Path targetFile =
        universeInfoHandler.downloadNodeFile(
            customer, universe, node, nodeHomeDir, sourceNodeFiles, nodeTargetFile);
    try {
      if (Files.exists(targetFile)) {
        File unZippedFile =
            unGzip(
                new File(targetFile.toAbsolutePath().toString()),
                new File(bundlePath.toAbsolutePath().toString()));
        Files.delete(targetFile);
        unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
        unZippedFile.delete();
      } else {
        log.debug(
            String.format(
                "No files exist at the source path '%s' for universe '%s' for component '%s'.",
                nodeHomeDir, universe.name, componentName));
      }
    } catch (Exception e) {
      log.error(
          String.format(
              "Something went wrong while trying to untar the files from "
                  + "component '%s' in the DB node: ",
              componentName),
          e);
    }
  }
}

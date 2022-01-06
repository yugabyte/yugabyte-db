// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.PlacementInfoUtil.getNumMasters;
import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModel;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.UnknownHostException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.TimeZone;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import lombok.Getter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class Util {
  public static final Logger LOG = LoggerFactory.getLogger(Util.class);
  private static final Map<UUID, Process> processMap = new ConcurrentHashMap<>();

  public static final String DEFAULT_YSQL_USERNAME = "yugabyte";
  public static final String DEFAULT_YSQL_PASSWORD = "yugabyte";
  public static final String DEFAULT_YSQL_ADMIN_ROLE_NAME = "yb_superuser";
  public static final String DEFAULT_YCQL_USERNAME = "cassandra";
  public static final String DEFAULT_YCQL_PASSWORD = "cassandra";
  public static final String YUGABYTE_DB = "yugabyte";
  public static final int MIN_NUM_BACKUPS_TO_RETAIN = 3;
  public static final String REDACT = "REDACTED";
  public static final String KEY_LOCATION_SUFFIX = "/backup_keys.json";
  public static final String SYSTEM_PLATFORM_DB = "system_platform";

  public static final String AZ = "AZ";
  public static final String GCS = "GCS";
  public static final String S3 = "S3";
  public static final String NFS = "NFS";

  public static final String BLACKLIST_LEADERS = "yb.upgrade.blacklist_leaders";
  public static final String BLACKLIST_LEADER_WAIT_TIME_MS =
      "yb.upgrade.blacklist_leader_wait_time_ms";

  /**
   * Returns a list of Inet address objects in the proxy tier. This is needed by Cassandra clients.
   */
  public static List<InetSocketAddress> getNodesAsInet(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    List<InetSocketAddress> inetAddrs = new ArrayList<>();
    for (String address : universe.getYQLServerAddresses().split(",")) {
      String[] splitAddress = address.split(":");
      String privateIp = splitAddress[0];
      int yqlRPCPort = Integer.parseInt(splitAddress[1]);
      inetAddrs.add(new InetSocketAddress(privateIp, yqlRPCPort));
    }
    return inetAddrs;
  }

  public static String redactString(String input) {
    String length = ((Integer) input.length()).toString();
    String regex = "(.)" + "{" + length + "}";
    String output = input.replaceAll(regex, REDACT);
    return output;
  }

  /**
   * Returns UUID representation of ID string without dashes For eg.
   * 87d2d6473b3645f7ba56d9e3f7dae239 becomes 87d2d647-3b36-45f7-ba56-d9e3f7dae239
   */
  public static UUID getUUIDRepresentation(String id) {
    if (id.length() != 32 || id.contains("-")) {
      return null;
    } else {
      String uuidWithHyphens =
          id.replaceAll("(\\w{8})(\\w{4})(\\w{4})(\\w{4})(\\w{12})", "$1-$2-$3-$4-$5");
      return UUID.fromString(uuidWithHyphens);
    }
  }

  /**
   * Returns a map of nodes in the ToBeAdded state in the given set of nodes.
   *
   * @param nodes nodes to examine for the Create/Edit operation
   * @return Map of AZUUID to number of desired nodes in the AZ
   */
  public static HashMap<UUID, Integer> toBeAddedAzUuidToNumNodes(Collection<NodeDetails> nodes) {
    HashMap<UUID, Integer> toBeAddedAzUUIDToNumNodes = new HashMap<>();
    if (nodes == null || nodes.isEmpty()) {
      return toBeAddedAzUUIDToNumNodes;
    }
    for (NodeDetails currentNode : nodes) {
      if (currentNode.state == NodeDetails.NodeState.ToBeAdded) {
        UUID currentAZUUID = currentNode.azUuid;
        toBeAddedAzUUIDToNumNodes.put(
            currentAZUUID, toBeAddedAzUUIDToNumNodes.getOrDefault(currentAZUUID, 0) + 1);
      }
    }
    return toBeAddedAzUUIDToNumNodes;
  }

  /**
   * Create a custom node prefix name from the given parameters.
   *
   * @param custId customer id owing the universe.
   * @param univName universe name.
   * @return The custom node prefix name.
   */
  public static String getNodePrefix(Long custId, String univName) {
    Customer c = Customer.find.query().where().eq("id", custId).findOne();
    if (c == null) {
      throw new RuntimeException("Invalid Customer Id: " + custId);
    }
    return String.format("yb-%s-%s", c.code, univName);
  }

  /**
   * Method returns a map of azUUID to number of master's per AZ.
   *
   * @param nodeDetailsSet The nodeDetailSet in the universe where masters are to be mapped.
   * @return Map of azUUID to numMastersInAZ.
   */
  private static Map<UUID, Integer> getMastersToAZMap(Collection<NodeDetails> nodeDetailsSet) {
    Map<UUID, Integer> mastersToAZMap = new HashMap<>();
    for (NodeDetails currentNode : nodeDetailsSet) {
      if (currentNode.isMaster) {
        mastersToAZMap.put(
            currentNode.azUuid, mastersToAZMap.getOrDefault(currentNode.azUuid, 0) + 1);
      } else {
        mastersToAZMap.putIfAbsent(currentNode.azUuid, 0);
      }
    }
    LOG.info("Masters to AZ :" + mastersToAZMap);
    return mastersToAZMap;
  }

  /*
   * Helper function to check if the set of nodes are in a single AZ or spread
   * across multiple AZ's
   */
  public static boolean isSingleAZ(Collection<NodeDetails> nodeDetailsSet) {
    UUID firstAZ = null;
    for (NodeDetails node : nodeDetailsSet) {
      UUID azUuid = node.azUuid;
      if (firstAZ == null) {
        firstAZ = azUuid;
        continue;
      }

      if (!firstAZ.equals(azUuid)) {
        return false;
      }
    }

    return true;
  }

  /**
   * API detects if addition of a master to the same AZ of current node makes master quorum get
   * closer to satisfying the replication factor requirements.
   *
   * @param currentNode the node whose AZ is checked.
   * @param nodeDetailsSet collection of nodes in a universe.
   * @param numMastersToBeAdded number of masters to be added.
   * @return true if starting a master on the node will enhance master replication of the universe.
   */
  @VisibleForTesting
  static boolean needMasterQuorumRestore(
      NodeDetails currentNode, Set<NodeDetails> nodeDetailsSet, long numMastersToBeAdded) {
    Map<UUID, Integer> mastersToAZMap = getMastersToAZMap(nodeDetailsSet);

    // If this is a single AZ deploy or if no master in current AZ, then start a
    // master.
    if (isSingleAZ(nodeDetailsSet) || mastersToAZMap.get(currentNode.azUuid) == 0) {
      return true;
    }

    Map<UUID, Integer> azToNumStoppedNodesMap = getAZToStoppedNodesCountMap(nodeDetailsSet);
    int numStoppedMasters = 0;
    for (UUID azUUID : azToNumStoppedNodesMap.keySet()) {
      if (azUUID != currentNode.azUuid
          && (!mastersToAZMap.containsKey(azUUID) || mastersToAZMap.get(azUUID) == 0)) {
        numStoppedMasters++;
      }
    }
    LOG.info("Masters: numStopped {}, numToBeAdded {}", numStoppedMasters, numMastersToBeAdded);

    return numStoppedMasters < numMastersToBeAdded;
  }

  /**
   * Method returns a map of azuuid to number of nodes stopped per az.
   *
   * @param nodeDetailsSet The set of nodes that need to be mapped.
   * @return Map of azUUID to num stopped nodes in that AZ.
   */
  private static Map<UUID, Integer> getAZToStoppedNodesCountMap(Set<NodeDetails> nodeDetailsSet) {
    Map<UUID, Integer> azToNumStoppedNodesMap = new HashMap<>();
    for (NodeDetails currentNode : nodeDetailsSet) {
      if (currentNode.state == NodeDetails.NodeState.Stopped
          || currentNode.state == NodeDetails.NodeState.Removed
          || currentNode.state == NodeDetails.NodeState.Decommissioned) {
        azToNumStoppedNodesMap.put(
            currentNode.azUuid, azToNumStoppedNodesMap.getOrDefault(currentNode.azUuid, 0) + 1);
      }
    }
    LOG.info("AZ to stopped count {}", azToNumStoppedNodesMap);
    return azToNumStoppedNodesMap;
  }

  /**
   * Checks if the universe needs a new master spawned on the current node.
   *
   * @param currentNode candidate node to be used to potentially spawn a master.
   * @param universe Universe to check for under replicated masters.
   * @return true if universe has fewer number of masters than RF.
   */
  public static boolean areMastersUnderReplicated(NodeDetails currentNode, Universe universe) {
    Cluster cluster = universe.getCluster(currentNode.placementUuid);
    if ((cluster == null) || (cluster.clusterType != ClusterType.PRIMARY)) {
      return false;
    }

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
    long numMasters = getNumMasters(nodes);
    int replFactor = universeDetails.getPrimaryCluster().userIntent.replicationFactor;
    LOG.info("RF = {} , numMasters = {}", replFactor, numMasters);

    return replFactor > numMasters
        && needMasterQuorumRestore(currentNode, nodes, replFactor - numMasters);
  }

  public static String UNIV_NAME_ERROR_MESG =
      "Invalid universe name format, valid characters [a-zA-Z0-9-].";

  // Validate the universe name pattern.
  public static boolean isValidUniverseNameFormat(String univName) {
    return univName.matches("^[a-zA-Z0-9-]*$");
  }

  // Helper API to create a CSV of any keys present in existing map but not in new
  // map.
  public static String getKeysNotPresent(Map<String, String> existing, Map<String, String> newMap) {
    Set<String> keysNotPresent = new HashSet<>();
    Set<String> existingKeySet = existing.keySet();
    Set<String> newKeySet = newMap.keySet();
    for (String key : existingKeySet) {
      if (!newKeySet.contains(key)) {
        keysNotPresent.add(key);
      }
    }
    LOG.info("KeysNotPresent  = " + keysNotPresent);

    return String.join(",", keysNotPresent);
  }

  public static JsonNode convertStringToJson(String inputString) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.readTree(inputString);
    } catch (IOException e) {
      throw new RuntimeException("I/O error reading json");
    }
  }

  public static String buildURL(String host, String endpoint) {
    try {
      return new URL("https", host, endpoint).toString();
    } catch (MalformedURLException e) {
      LOG.error("Error building request URL", e);

      return null;
    }
  }

  public static String unixTimeToString(long epochSec) {
    Date date = new Date(epochSec * 1000);
    SimpleDateFormat format = new SimpleDateFormat();

    return format.format(date);
  }

  public static void writeStringToFile(File file, String contents) throws Exception {
    try (FileWriter writer = new FileWriter(file)) {
      writer.write(contents);
    }
  }

  /**
   * Extracts the name and extension parts of a file name.
   *
   * <p>The resulting string is the rightmost characters of fullName, starting with the first
   * character after the path separator that separates the path information from the name and
   * extension.
   *
   * <p>The resulting string is equal to fullName, if fullName contains no path.
   *
   * @param fullName
   * @return
   */
  public static String getFileName(String fullName) {
    if (fullName == null) {
      return null;
    }
    int delimiterIndex = fullName.lastIndexOf(File.separatorChar);
    return delimiterIndex >= 0 ? fullName.substring(delimiterIndex + 1) : fullName;
  }

  public static String getFileChecksum(String file) throws IOException, NoSuchAlgorithmException {
    FileInputStream fis = new FileInputStream(file);
    byte[] byteArray = new byte[1024];
    int bytesCount = 0;

    MessageDigest digest = MessageDigest.getInstance("MD5");

    while ((bytesCount = fis.read(byteArray)) != -1) {
      digest.update(byteArray, 0, bytesCount);
    }

    fis.close();

    byte[] bytes = digest.digest();
    StringBuilder sb = new StringBuilder();
    for (byte b : bytes) {
      sb.append(Integer.toString((b & 0xff) + 0x100, 16).substring(1));
    }
    return sb.toString();
  }

  public static List<File> listFiles(Path backupDir, String pattern) throws IOException {
    try (DirectoryStream<Path> directoryStream = Files.newDirectoryStream(backupDir, pattern)) {
      return StreamSupport.stream(directoryStream.spliterator(), false)
          .map(Path::toFile)
          .sorted(File::compareTo)
          .collect(Collectors.toList());
    }
  }

  public static void moveFile(Path source, Path destination) throws IOException {
    Files.move(source, destination, REPLACE_EXISTING);
  }

  public static void writeJsonFile(String filePath, ArrayNode json) {
    writeFile(filePath, Json.prettyPrint(json));
  }

  public static void writeFile(String filePath, String contents) {
    try (FileWriter file = new FileWriter(filePath)) {
      file.write(contents);
      file.flush();
      LOG.info("Written: {}", filePath);
    } catch (IOException e) {
      LOG.error("Unable to write: {}", filePath);
      throw new RuntimeException(e.getMessage());
    }
  }

  /**
   * @deprecated Avoid using request body with Json ArrayNode as root. This is because
   *     for @ApiImplicitParam does not support that. Instead create a top level request object that
   *     wraps the array If at all, use this only for undocumented API
   */
  @Deprecated
  public static <T> List<T> parseJsonArray(String content, Class<T> elementType) {
    try {
      return Json.mapper()
          .readValue(
              content,
              Json.mapper().getTypeFactory().constructCollectionType(List.class, elementType));
    } catch (IOException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Failed to parse List<"
              + elementType.getSimpleName()
              + ">"
              + " object: "
              + content
              + " error: "
              + e.getMessage());
    }
  }

  @ApiModel(value = "UniverseDetailSubset", description = "A small subset of universe information")
  @Getter
  public static class UniverseDetailSubset {
    final UUID uuid;
    final String name;
    final boolean updateInProgress;
    final boolean updateSucceeded;
    final long creationDate;
    final boolean universePaused;

    public UniverseDetailSubset(Universe universe) {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      uuid = universe.universeUUID;
      name = universe.name;
      updateInProgress = universeDetails.updateInProgress;
      updateSucceeded = universeDetails.updateSucceeded;
      creationDate = universe.creationDate.getTime();
      universePaused = universeDetails.universePaused;
    }
  }

  public static List<UniverseDetailSubset> getUniverseDetails(Set<Universe> universes) {
    List<UniverseDetailSubset> details = new ArrayList<>();
    for (Universe universe : universes) {
      details.add(new UniverseDetailSubset(universe));
    }
    return details;
  }

  public static int compareYbVersions(String v1, String v2) {
    Pattern versionPattern = Pattern.compile("^(\\d+.\\d+.\\d+.\\d+)(-(b(\\d+)|(\\w+)))?$");
    Matcher v1Matcher = versionPattern.matcher(v1);
    Matcher v2Matcher = versionPattern.matcher(v2);

    if (v1Matcher.find() && v2Matcher.find()) {
      String[] v1Numbers = v1Matcher.group(1).split("\\.");
      String[] v2Numbers = v2Matcher.group(1).split("\\.");
      for (int i = 0; i < 4; i++) {
        int a = Integer.parseInt(v1Numbers[i]);
        int b = Integer.parseInt(v2Numbers[i]);
        if (a != b) {
          return a - b;
        }
      }

      String v1BuildNumber = v1Matcher.group(4);
      String v2BuildNumber = v2Matcher.group(4);
      // If one of the build number is null (i.e local build) then consider
      // versions as equal as we cannot compare between local builds
      // e.g: 2.5.2.0-b15 and 2.5.2.0-custom are considered equal
      // 2.5.2.0-custom1 and 2.5.2.0-custom2 are considered equal too
      if (v1BuildNumber != null && v2BuildNumber != null) {
        int a = Integer.parseInt(v1BuildNumber);
        int b = Integer.parseInt(v2BuildNumber);
        return a - b;
      }

      return 0;
    }

    throw new RuntimeException("Unable to parse YB version strings");
  }

  public static String escapeSingleQuotesOnly(String src) {
    return src.replaceAll("'", "''");
  }

  @VisibleForTesting
  public static String removeEnclosingDoubleQuotes(String src) {
    if (src != null && src.startsWith("\"") && src.endsWith("\"")) {
      return src.substring(1, src.length() - 1);
    }
    return src;
  }

  public static void setPID(UUID uuid, Process pid) {
    processMap.put(uuid, pid);
  }

  public static Process getProcessOrBadRequest(UUID uuid) {
    if (processMap.get(uuid) == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "The process you want to stop is not in progress.");
    }
    return processMap.get(uuid);
  }

  public static void removeProcess(UUID uuid) {
    processMap.remove(uuid);
  }

  // It can be inferred that Platform only supports Base64 encryption
  // for Slow Query Credentials for now
  public static String decodeBase64(String input) {
    byte[] decodedBytes = Base64.getDecoder().decode(input);
    return new String(decodedBytes);
  }

  public static String encodeBase64(String input) {
    return Base64.getEncoder().encodeToString(input.getBytes());
  }

  public static String doubleToString(double value) {
    return BigDecimal.valueOf(value).stripTrailingZeros().toPlainString();
  }

  // This will help us in insertion of set of keys in locked synchronized way as no
  // extraction/deletion action should be performed on RunTimeConfig object during the process.
  // TODO: Fix this locking static method - this locks whole Util class with unrelated methods.
  //  This should really be using database transactions since runtime config is persisted.
  public static synchronized void setLockedMultiKeyConfig(
      RuntimeConfig<Universe> config, Map<String, String> configKeysMap) {
    configKeysMap.forEach(
        (key, value) -> {
          config.setValue(key, value);
        });
  }

  // This will help us in extraction of set of keys in locked synchronized way as no
  // insertion/deletion action should be performed on RunTimeConfig object during the process.
  public static synchronized Map<String, String> getLockedMultiKeyConfig(
      RuntimeConfig<Universe> config, List<String> configKeys) {
    Map<String, String> configKeysMap = new HashMap<>();
    configKeys.forEach((key) -> configKeysMap.put(key, config.getString(key)));
    return configKeysMap;
  }

  // This will help us in deletion of set of keys in locked synchronized way as no
  // insertion/extraction action should be performed on RunTimeConfig object during the process.
  public static synchronized void deleteLockedMultiKeyConfig(
      RuntimeConfig<Universe> config, List<String> configKeys) {
    configKeys.forEach(
        (key) -> {
          if (config.hasPath(key)) {
            config.deleteEntry(key);
          }
        });
  }

  /** deleteDirectory deletes entire directory recursively. */
  public static boolean deleteDirectory(File directoryToBeDeleted) {
    File[] allContents = directoryToBeDeleted.listFiles();
    if (allContents != null) {
      for (File file : allContents) {
        deleteDirectory(file);
      }
    }
    return directoryToBeDeleted.delete();
  }

  /**
   * Returns the Unix epoch timeStamp in microseconds provided the given timeStamp and it's format.
   */
  public static long microUnixTimeFromDateString(String timeStamp, String timeStampFormat)
      throws ParseException {
    SimpleDateFormat format = new SimpleDateFormat(timeStampFormat);
    try {
      long timeStampUnix = format.parse(timeStamp).getTime() * 1000L;
      return timeStampUnix;
    } catch (ParseException e) {
      throw e;
    }
  }

  public static String unixTimeToDateString(long unixTimestampMs, String dateFormat) {
    SimpleDateFormat formatter = new SimpleDateFormat(dateFormat);
    return formatter.format(new Date(unixTimestampMs));
  }

  public static String unixTimeToDateString(long unixTimestampMs, String dateFormat, TimeZone tz) {
    SimpleDateFormat formatter = new SimpleDateFormat(dateFormat);
    formatter.setTimeZone(tz);
    return formatter.format(new Date(unixTimestampMs));
  }

  // Update the Universe's 'backupInProgress' flag to new state in synchronized manner to avoid
  // race condition.
  public static synchronized void lockedUpdateBackupState(
      UUID universeUUID, UniverseTaskBase backupTask, boolean newState) {
    if (Universe.getOrBadRequest(universeUUID).getUniverseDetails().backupInProgress == newState) {
      if (newState) {
        throw new RuntimeException("A backup for this universe is already in progress.");
      } else {
        return;
      }
    }
    backupTask.updateBackupState(newState);
  }

  public static String getHostname() {
    try {
      return InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
      return "";
    }
  }

  public static String getHostIP() {
    try {
      return InetAddress.getLocalHost().getHostAddress().toString();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the host IP", e);
      return "";
    }
  }

  public static InputStream getInputStreamOrFail(File file) {
    try {
      return new FileInputStream(file);
    } catch (FileNotFoundException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public static String getNodeIp(Universe universe, NodeDetails node) {
    String ip = null;
    if (node.cloudInfo == null || node.cloudInfo.private_ip == null) {
      NodeDetails onDiskNode = universe.getNode(node.nodeName);
      ip = onDiskNode.cloudInfo.private_ip;
    } else {
      ip = node.cloudInfo.private_ip;
    }
    return ip;
  }
}

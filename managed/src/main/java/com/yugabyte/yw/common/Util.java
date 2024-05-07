// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.google.common.base.Preconditions.checkArgument;
import static com.yugabyte.yw.common.PlacementInfoUtil.getNumMasters;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.OsType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModel;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.MalformedURLException;
import java.net.Socket;
import java.net.URL;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TimeZone;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.GZIPInputStream;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;
import org.apache.commons.codec.binary.Hex;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.InetAddressValidator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class Util {
  public static final Logger LOG = LoggerFactory.getLogger(Util.class);
  private static final Map<UUID, Process> processMap = new ConcurrentHashMap<>();

  public static final UUID NULL_UUID = UUID.fromString("00000000-0000-0000-0000-000000000000");
  public static final String YSQL_PASSWORD_KEYWORD = "PASSWORD";
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
  public static final int YB_SCHEDULER_INTERVAL = 2;
  public static final String DEFAULT_YB_SSH_USER = "yugabyte";
  public static final String DEFAULT_SUDO_SSH_USER = "centos";

  public static final String AZ = "AZ";
  public static final String GCS = "GCS";
  public static final String S3 = "S3";
  public static final String NFS = "NFS";

  public static final String CUSTOMERS = "customers";
  public static final String UNIVERSES = "universes";
  public static final String USERS = "users";
  public static final String ROLE = "role";
  public static final String UNIVERSE_UUID = "universeUUID";

  public static final String AVAILABLE_MEMORY = "MemAvailable";

  public static final String UNIVERSE_NAME_REGEX = "^[a-zA-Z0-9]([-a-zA-Z0-9]*[a-zA-Z0-9])?$";

  public static final double EPSILON = 0.000001d;

  public static final String K8S_YBC_COMPATIBLE_DB_VERSION = "2.17.3.0-b62";

  public static final String YBDB_ROLLBACK_DB_VERSION = "2.20.2.0-b1";

  public static final String AUTO_FLAG_FILENAME = "auto_flags.json";

  public static final String DB_VERSION_METADATA_FILENAME = "version_metadata.json";

  public static final String LIVE_QUERY_TIMEOUTS = "yb.query_stats.live_queries.ws";

  public static final String YB_RELEASES_PATH = "yb.releases.path";

  public static final String YB_NODE_UI_WS_KEY = "yb.node_ui.ws";

  public static final String K8S_POD_FQDN_TEMPLATE =
      "{pod_name}.{service_name}.{namespace}.svc.{cluster_domain}";

  public static final String YBA_VERSION_REGEX = "^(\\d+.\\d+.\\d+.\\d+)(-(b(\\d+)|(\\w+)))?$";

  private static final Map<String, Long> GO_DURATION_UNITS_TO_NANOS =
      ImmutableMap.<String, Long>builder()
          .put("s", TimeUnit.SECONDS.toNanos(1))
          .put("m", TimeUnit.MINUTES.toNanos(1))
          .put("h", TimeUnit.HOURS.toNanos(1))
          .put("d", TimeUnit.DAYS.toNanos(1))
          .put("ms", TimeUnit.MILLISECONDS.toNanos(1))
          .put("us", TimeUnit.MICROSECONDS.toNanos(1))
          .put("\u00b5s", TimeUnit.MICROSECONDS.toNanos(1))
          .put("ns", 1L)
          .build();

  private static final Pattern GO_DURATION_REGEX =
      Pattern.compile("(\\d+)(ms|us|\\u00b5s|ns|s|m|h|d)");

  public static final String HTTP_SCHEME = "http://";

  public static final String HTTPS_SCHEME = "https://";

  public static volatile String YBA_VERSION;

  public static String getYbaVersion() {
    return YBA_VERSION;
  }

  public static void setYbaVersion(String version) {
    YBA_VERSION = version;
  }
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

  public static String redactYsqlQuery(String input) {
    return input.replaceAll(
        YSQL_PASSWORD_KEYWORD + " (.+?)';", String.format("%s %s;", YSQL_PASSWORD_KEYWORD, REDACT));
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
    return String.format("yb-%s-%s", c.getCode(), univName);
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

  public static String UNIVERSE_NAME_ERROR_MESG =
      String.format(
          "Invalid universe name format, regex used for validation is %s.", UNIVERSE_NAME_REGEX);

  // Validate the universe name pattern.
  public static boolean isValidUniverseNameFormat(String univName) {
    return univName.matches(UNIVERSE_NAME_REGEX);
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
  @Value
  @Jacksonized
  @Builder
  @AllArgsConstructor
  public static class UniverseDetailSubset {
    UUID uuid;
    String name;
    boolean updateInProgress;
    boolean updateSucceeded;
    long creationDate;
    boolean universePaused;

    public UniverseDetailSubset(Universe universe) {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      uuid = universe.getUniverseUUID();
      name = universe.getName();
      updateInProgress = universeDetails.updateInProgress;
      updateSucceeded = universeDetails.updateSucceeded;
      creationDate = universe.getCreationDate().getTime();
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

  // Wrapper on the existing compareYbVersions() method (to specify if format error
  // should be suppressed)
  public static int compareYbVersions(String v1, String v2) {

    return compareYbVersions(v1, v2, false);
  }

  // Compare v1 and v2 Strings. Returns 0 if the versions are equal, a
  // positive integer if v1 is newer than v2, a negative integer if v1
  // is older than v2.
  public static int compareYbVersions(String v1, String v2, boolean suppressFormatError) {
    // After the second dash, a user can add anything, and it will be ignored.
    String[] v1Parts = v1.split("-", 3);
    if (v1Parts.length > 2) {
      v1 = v1Parts[0] + "-" + v1Parts[1];
    }
    String[] v2Parts = v2.split("-", 3);
    if (v2Parts.length > 2) {
      v2 = v2Parts[0] + "-" + v2Parts[1];
    }

    Pattern versionPattern = Pattern.compile(YBA_VERSION_REGEX);
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

    if (suppressFormatError) {

      // If suppressFormat Error is true and the YB version strings
      // are unable to be parsed, we output the log for debugging purposes
      // and simply consider the versions as equal (similar to the custom
      // build logic above).

      String msg =
          String.format(
              "At least one YB version string out of %s and %s is unable to be parsed."
                  + " The two versions are treated as equal because"
                  + " suppressFormatError is set to true.",
              v1, v2);

      LOG.info(msg);

      return 0;
    }

    throw new RuntimeException("Unable to parse YB version strings");
  }

  public static void ensureYbVersionFormatValidOrThrow(String ybVersion) {
    // Phony comparison to check the version format.
    compareYbVersions(ybVersion, "0.0.0.0-b0", false /* suppressFormatError */);
  }

  public static boolean isYbVersionFormatValid(String ybVersion) {
    try {
      ensureYbVersionFormatValidOrThrow(ybVersion);
      return true;
    } catch (Exception ignore) {
      return false;
    }
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

  // Generate a deterministic node UUID from the universe UUID and the node name.
  public static UUID generateNodeUUID(UUID universeUuid, String nodeName) {
    return UUID.nameUUIDFromBytes((universeUuid.toString() + nodeName).getBytes());
  }

  // Generate hash string of given length for a given name.
  // As each byte is represented by two hex chars, the length doubles.
  public static String hashString(String name) {
    int hashCode = name.hashCode();
    byte[] bytes = ByteBuffer.allocate(4).putInt(hashCode).array();
    return Hex.encodeHexString(bytes);
  }

  // Converts input string to base36 string of length 4.
  // return string will contain characters a-z, 0-9.
  public static String base36hash(String inputStr) {
    int hashCode = inputStr.hashCode();
    byte[] bytes = new byte[4];
    char[] chars = new char[4];
    for (int i = 0; i < bytes.length; i++) {
      // 1 byte.
      int val = hashCode & 0xFF;
      if (val >= 0 && val <= 9) {
        chars[i] = (char) ('0' + val);
      } else {
        chars[i] = (char) ('a' + (val - 10) % 26);
      }
      hashCode >>= 8;
    }
    return new String(chars);
  }

  // Sanitize kubernetes namespace name. Additional suffix length can be reserved.
  // Valid namespaces are not modified for backward compatibility.
  // Only the non-conforming ones which have passed the UNIVERSE_NAME_REGEX are sanitized.
  public static String sanitizeKubernetesNamespace(String name, int reserveSuffixLen) {
    // Max allowed namespace length is 63.
    int maxNamespaceLen = 63;
    int firstPartLength = maxNamespaceLen - reserveSuffixLen;
    checkArgument(firstPartLength > 0, "Invalid suffix length");
    String sanitizedName = name.toLowerCase();
    if (sanitizedName.equals(name) && firstPartLength >= sanitizedName.length()) {
      // Backward compatibility taken care as old namespaces must have already passed this test for
      // k8s.
      return name;
    }
    // Decrease by 8 hash hex chars + 1 dash(-).
    firstPartLength -= 9;
    checkArgument(firstPartLength > 0, "Invalid suffix length");
    if (sanitizedName.length() > firstPartLength) {
      sanitizedName = sanitizedName.substring(0, firstPartLength);
      LOG.warn("Name {} is longer than {}, truncated to {}.", name, firstPartLength, sanitizedName);
    }
    return String.format("%s-%s", sanitizedName, hashString(name));
  }

  public static boolean canConvertJsonNode(JsonNode jsonNode, Class<?> toValueType) {
    try {
      Json.mapper().treeToValue(jsonNode, toValueType);
    } catch (JsonProcessingException e) {
      LOG.info(e.getMessage());
      return false;
    }
    return true;
  }

  public static boolean doubleEquals(double d1, double d2) {
    return Math.abs(d1 - d2) < Util.EPSILON;
  }

  /** Checks if the given date is past the current time or not. */
  public static boolean isTimeExpired(Date date) {
    Date currentTime = new Date();
    return currentTime.compareTo(date) >= 0 ? true : false;
  }

  public static synchronized Path getOrCreateDir(Path dirPath) {
    // Parent of path ending with a path component separator is the path itself.
    File dir = dirPath.toFile();
    if (!dir.exists() && !dir.mkdirs() && !dir.exists()) {
      throw new RuntimeException("Failed to create " + dirPath);
    }
    return dirPath;
  }

  public static String getNodeHomeDir(UUID universeUUID, NodeDetails node) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String providerUUID = universe.getCluster(node.placementUuid).userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerUUID));
    return provider.getYbHome();
  }

  public static boolean isOnPremManualProvisioning(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.providerType == Common.CloudType.onprem) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
      return provider.getDetails().skipProvisioning;
    }
    return false;
  }

  /**
   * @param ybServerPackage
   * @return pair of string containing osType and archType of ybc-server-package
   */
  public static Pair<String, String> getYbcPackageDetailsFromYbServerPackage(
      String ybServerPackage) {
    String archType = null;
    if (ybServerPackage.contains(Architecture.x86_64.name().toLowerCase())) {
      archType = Architecture.x86_64.name();
    } else if (ybServerPackage.contains(Architecture.aarch64.name().toLowerCase())) {
      archType = Architecture.aarch64.name();
    } else {
      throw new RuntimeException(
          "Cannot install ybc on machines of arch types other than x86_64, aarch64");
    }

    // We are using standard open-source OS names in case of different arch.
    String osType = OsType.LINUX.toString();
    if (!archType.equals(Architecture.x86_64.name())) {
      osType = OsType.EL8.toString();
    }
    return new Pair<>(osType.toLowerCase(), archType.toLowerCase());
  }

  /**
   * Basic DNS address check which allows only alphanumeric characters and hyphen (-) in the name.
   * Hyphen cannot be at the beginning or at the end of a DNS label.
   */
  public static boolean isValidDNSAddress(String dns) {
    return dns.matches("^((?!-)[A-Za-z0-9-]+(?<!-)\\.)+[A-Za-z]+$");
  }

  public static Duration goDurationToJava(String goDuration) {
    if (StringUtils.isEmpty(goDuration)) {
      throw new IllegalArgumentException("Duration string can't be empty");
    }
    Matcher m = GO_DURATION_REGEX.matcher(goDuration);
    boolean found = false;
    long nanos = 0;
    while (m.find()) {
      found = true;
      long amount = Long.parseLong(m.group(1));
      String unit = m.group(2);
      long multiplier = GO_DURATION_UNITS_TO_NANOS.get(unit);
      nanos += amount * multiplier;
    }
    if (!found) {
      throw new IllegalArgumentException("Duration string " + goDuration + " is invalid");
    }
    return Duration.ofNanos(nanos);
  }

  /**
   * Adds the file/directory from filePath to the archive tarArchive starting at the parent location
   * in the archive
   *
   * @param filePath the directory/file we want to add to the archive.
   * @param parent the location where we are storing the directory/file in the archive, "" is the
   *     root of the archive
   * @param tarArchive the archive we want the directory/file be added to.
   */
  public static void addFilesToTarGZ(
      String filePath, String parent, TarArchiveOutputStream tarArchive) throws IOException {
    File file = new File(filePath);
    String entryName = parent + file.getName();
    tarArchive.putArchiveEntry(new TarArchiveEntry(file, entryName));
    if (file.isFile()) {
      try (FileInputStream fis = new FileInputStream(file);
          BufferedInputStream bis = new BufferedInputStream(fis)) {
        IOUtils.copy(bis, tarArchive);
        tarArchive.closeArchiveEntry();
      }
    } else if (file.isDirectory()) {
      // no content to copy so close archive entry
      tarArchive.closeArchiveEntry();
      for (File f : file.listFiles()) {
        addFilesToTarGZ(f.getAbsolutePath(), entryName + File.separator, tarArchive);
      }
    }
  }

  /**
   * Adds the the file archive tarArchive in fileName
   *
   * @param file the file we want to add to the archive
   * @param fileName the location we want the file to be saved in the archive
   * @param tarArchive the archive we want the file be added to.
   */
  public static void copyFileToTarGZ(File file, String fileName, TarArchiveOutputStream tarArchive)
      throws IOException {
    tarArchive.putArchiveEntry(tarArchive.createArchiveEntry(file, fileName));
    try (FileInputStream fis = new FileInputStream(file);
        BufferedInputStream bis = new BufferedInputStream(fis)) {
      IOUtils.copy(bis, tarArchive);
      tarArchive.closeArchiveEntry();
    }
  }

  /**
   * Extracts the archive tarFile and untars to into folderPath directory
   *
   * @param tarFile the archive we want to sasve to folderPath
   * @param folderPath the directory where we want to extract the archive to
   */
  public static void extractFilesFromTarGZ(Path tarFile, String folderPath) throws IOException {
    TarArchiveEntry currentEntry;
    Files.createDirectories(Paths.get(folderPath));

    try (FileInputStream fis = new FileInputStream(tarFile.toFile());
        GZIPInputStream gis = new GZIPInputStream(new BufferedInputStream(fis));
        TarArchiveInputStream tis = new TarArchiveInputStream(gis)) {
      while ((currentEntry = tis.getNextTarEntry()) != null) {
        File destPath = new File(folderPath, currentEntry.getName());
        if (currentEntry.isDirectory()) {
          destPath.mkdirs();
        } else {
          destPath.createNewFile();
          try (FileOutputStream fos = new FileOutputStream(destPath)) {
            IOUtils.copy(tis, fos);
          }
        }
      }
    }
  }

  public static boolean isKubernetesBasedUniverse(Universe universe) {
    boolean isKubernetesUniverse =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes);
    for (Cluster cluster : universe.getUniverseDetails().getReadOnlyClusters()) {
      isKubernetesUniverse =
          isKubernetesUniverse || cluster.userIntent.providerType.equals(CloudType.kubernetes);
    }
    return isKubernetesUniverse;
  }

  public static String getYbcNodeIp(Universe universe) {
    List<NodeDetails> nodeList = universe.getRunningTserversInPrimaryCluster();
    return nodeList.get(0).cloudInfo.private_ip;
  }

  public static String computeFileChecksum(Path filePath, String checksumAlgorithm)
      throws Exception {
    checksumAlgorithm = checksumAlgorithm.toUpperCase();
    if (checksumAlgorithm.equals("SHA1")) {
      checksumAlgorithm = "SHA-1";
    } else if (checksumAlgorithm.equals("SHA256")) {
      checksumAlgorithm = "SHA-256";
    }
    MessageDigest md = MessageDigest.getInstance(checksumAlgorithm);
    try (DigestInputStream dis =
        new DigestInputStream(new FileInputStream(filePath.toFile()), md)) {
      while (dis.read() != -1) ; // Empty loop to clear the data
      md = dis.getMessageDigest();
      // Convert the digest to String.
      StringBuilder result = new StringBuilder();
      for (byte b : md.digest()) {
        result.append(String.format("%02x", b));
      }
      return result.toString().toLowerCase();
    }
  }

  public static String maybeGetEmailFromContext() {
    return Optional.ofNullable(RequestContext.getIfPresent(TokenAuthenticator.USER))
        .map(UserWithFeatures::getUser)
        .map(Users::getEmail)
        .map(Object::toString)
        .orElse("Unknown");
  }

  public static Universe lockUniverse(Universe universe) {
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          universeDetails.updateInProgress = true;
          universeDetails.updateSucceeded = false;
          u.setUniverseDetails(universeDetails);
        };
    return Universe.saveDetails(universe.getUniverseUUID(), updater, false);
  }

  public static Universe unlockUniverse(Universe universe) {
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          universeDetails.updateInProgress = false;
          universeDetails.updateSucceeded = true;
          u.setUniverseDetails(universeDetails);
        };
    return Universe.saveDetails(universe.getUniverseUUID(), updater, false);
  }

  public static boolean isAddressReachable(String host, int port) {
    try (Socket socket = new Socket()) {
      socket.connect(new InetSocketAddress(host, port), 3000);
      return true;
    } catch (IOException e) {
    }
    return false;
  }

  public static long getExponentialBackoffDelayMs(
      long initialDelayMs, long maxDelayMs, int iterationNumber) {
    double multiplier = 2.0;
    long delay =
        (long) (initialDelayMs * Math.pow(multiplier, iterationNumber) + Math.random() * 1000);
    if (delay > maxDelayMs) {
      delay = maxDelayMs;
    }
    return delay;
  }

  /**
   * Gets the path to "yb-data/" folder on the node (Ex: "/mnt/d0", "/mnt/disk0")
   *
   * @param universe
   * @param node
   * @param config
   * @return the path to "yb-data/" folder on the node
   */
  public static String getDataDirectoryPath(Universe universe, NodeDetails node, Config config) {
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
        LOG.error(String.format("On prem invalid mount points. Defaulting to %s", dataDirPath), e);
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
        List<VolumeDetails> volumeDetailsList =
            instanceType.getInstanceTypeDetails().volumeDetailsList;
        if (CollectionUtils.isNotEmpty(volumeDetailsList)) {
          dataDirPath = volumeDetailsList.get(0).mountPath;
        } else {
          LOG.info("Mount point is not defined. Defaulting to {}", dataDirPath);
        }
      } catch (Exception e) {
        LOG.error(String.format("Could not get mount points. Defaulting to %s", dataDirPath), e);
      }
    }
    return dataDirPath;
  }

  public static String extractRegexValue(String input, String patternStr) {
    Pattern pattern = Pattern.compile(patternStr);
    Matcher matcher = pattern.matcher(input);
    if (matcher.find()) {
      return matcher.group(1);
    }
    return null;
  }

  public static boolean isIpAddress(String maybeIp) {
    InetAddressValidator ipValidator = InetAddressValidator.getInstance();
    return ipValidator.isValidInet4Address(maybeIp) || ipValidator.isValidInet6Address(maybeIp);
  }

  /**
   * Validate url string and get URL object
   *
   * @param url
   * @param defaultHttpScheme
   * @return
   */
  public static URL validateAndGetURL(String url, boolean defaultHttpScheme) {
    String urlString = url;
    if (!urlString.startsWith(HTTP_SCHEME) && !urlString.startsWith(HTTPS_SCHEME)) {
      urlString = (defaultHttpScheme ? HTTP_SCHEME : HTTPS_SCHEME) + urlString;
    }
    try {
      URL urlInstance = new URL(urlString);
      if (StringUtils.isBlank(urlInstance.getHost())) {
        throw new RuntimeException("Malformed URL: " + urlString);
      }
      return urlInstance;
    } catch (MalformedURLException e) {
      throw new RuntimeException("Malformed URL: " + urlString);
    }
  }

  public static UUID retreiveImageBundleUUID(
      Architecture arch, UserIntent userIntent, Provider provider) {
    return retreiveImageBundleUUID(arch, userIntent, provider, false);
  }

  public static UUID retreiveImageBundleUUID(
      Architecture arch, UserIntent userIntent, Provider provider, boolean cloudEnabled) {
    UUID imageBundleUUID = null;
    if (userIntent.imageBundleUUID != null) {
      imageBundleUUID = userIntent.imageBundleUUID;
    } else if (provider.getUuid() != null && !cloudEnabled) {
      // Don't use defaultProvider bundle for YBM, as they will
      // specify machineImage for provisioning the node.
      List<ImageBundle> bundles = ImageBundle.getDefaultForProvider(provider.getUuid());
      if (bundles.size() > 0) {
        ImageBundle bundle = ImageBundleUtil.getDefaultBundleForUniverse(arch, bundles);
        if (bundle != null) {
          imageBundleUUID = bundle.getUuid();
        }
      }
    }

    return imageBundleUUID;
  }
}

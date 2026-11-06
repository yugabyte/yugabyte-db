// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.AppenderBase;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.LocalNodeUniverseManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.ebean.DB;
import io.ebean.SqlRow;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class SensitiveGFlagsRedactionLocalTest extends LocalProviderUniverseTestBase {

  private static final String YSQL_PASSWORD = "Ysql-Pass-7Qx2";
  private static final String YCQL_LDAP_SECRET = "ycql-ldap-old-9Kd3";
  private static final String YCQL_LDAP_NEW_SECRET = "ycql-ldap-new-4Tz8";
  private static final String HBA_SECRET = "hba-ldap-secret-2Wm6";
  private static final List<String> SECRETS =
      List.of(YSQL_PASSWORD, YCQL_LDAP_SECRET, YCQL_LDAP_NEW_SECRET, HBA_SECRET);

  // An LDAP record for a database that doesn't exist, so YBA's own YSQL connections still fall
  // through to the md5 record the tserver appends.
  private static final String HBA_LDAP_RECORD =
      "host ldapdb all 0.0.0.0/0 ldap ldapserver=ldap.example.com"
          + " ldapbasedn=\"dc=example,dc=com\" ldapbinddn=\"cn=admin,dc=example,dc=com\""
          + " ldapbindpasswd="
          + HBA_SECRET;
  // CSV-quoted because the DNs hold commas, so the unquoted password sits right before the
  // closing quote.
  private static final String HBA_CONF_CSV = "\"" + HBA_LDAP_RECORD.replace("\"", "\"\"") + "\"";
  private static final String YCQL_LDAP_BIND_PASSWD = "ycql_ldap_bind_passwd";

  // The local provider's node and universe managers log node commands with their arguments. They
  // stand in for ansible and node agent, and don't run outside these tests.
  private static final Set<String> LOGGERS_ALLOWED_SECRETS =
      Set.of(
          LocalNodeManager.class.getName(),
          LocalNodeUniverseManager.class.getName(),
          SensitiveGFlagsRedactionLocalTest.class.getName());

  // Collected rather than asserted one by one, so a failure reports every place a secret leaked.
  private final List<String> problems = Collections.synchronizedList(new ArrayList<>());

  @Test
  public void testSensitiveGFlagsAcrossUiRoundTripUpgradeAndRetry() throws Exception {
    SecretLogScanner logScanner = new SecretLogScanner();
    Logger rootLogger =
        ((LoggerContext) LoggerFactory.getILoggerFactory()).getLogger(Logger.ROOT_LOGGER_NAME);
    logScanner.setContext(rootLogger.getLoggerContext());
    logScanner.start();
    rootLogger.addAppender(logScanner);
    try {
      runScenario();
    } finally {
      rootLogger.detachAppender(logScanner);
      logScanner.stop();
    }
    assertTrue(String.join("\n", problems), problems.isEmpty());
  }

  private void runScenario() throws Exception {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction.getKey(), "true");

    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent(null, true);
    userIntent.enableYSQLAuth = true;
    userIntent.ysqlPassword = YSQL_PASSWORD;
    Map<String, String> masterGFlags =
        new HashMap<>(userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER));
    Map<String, String> tserverGFlags =
        new HashMap<>(userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER));
    tserverGFlags.put(YCQL_LDAP_BIND_PASSWD, YCQL_LDAP_SECRET);
    tserverGFlags.put(GFlagsUtil.YSQL_HBA_CONF_CSV, HBA_CONF_CSV);
    userIntent.specificGFlags = SpecificGFlags.construct(masterGFlags, tserverGFlags);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UUID universeUUID = universe.getUniverseUUID();

    checkNodes("after create", universe, YCQL_LDAP_SECRET, Collections.emptyMap());
    checkStoredUniverse("after create", universeUUID, YCQL_LDAP_SECRET);

    // What the UI gets, and sends back with only the fields the user edited changed.
    JsonNode universeJson = getUniverseViaApi(universeUUID);
    JsonNode apiTserverFlags = tserverSpecificGFlags(universeJson.get("universeDetails"));
    check(
        SECRET_REPLACEMENT.equals(apiTserverFlags.path(YCQL_LDAP_BIND_PASSWD).asText()),
        "universe API ycql_ldap_bind_passwd: " + apiTserverFlags.path(YCQL_LDAP_BIND_PASSWD));
    String expectedHbaDisplay = HBA_CONF_CSV.replace(HBA_SECRET, SECRET_REPLACEMENT);
    check(
        expectedHbaDisplay.equals(apiTserverFlags.path(GFlagsUtil.YSQL_HBA_CONF_CSV).asText()),
        "universe API ysql_hba_conf_csv: "
            + apiTserverFlags.path(GFlagsUtil.YSQL_HBA_CONF_CSV)
            + " expected "
            + expectedHbaDisplay);
    checkNoSecret("universe API before upgrade", universeJson.toString());

    ObjectNode body = Json.newObject();
    body.set("clusters", universeJson.get("universeDetails").get("clusters").deepCopy());
    body.set("upgradeOption", Json.toJson(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE));
    body.put("expectedUniverseVersion", universeJson.get("version").asInt());
    body.put("sleepAfterTServerRestartMillis", 10000);
    body.put("sleepAfterMasterRestartMillis", 10000);
    ObjectNode requestTserverFlags = (ObjectNode) tserverSpecificGFlags(body);
    requestTserverFlags.put(YCQL_LDAP_BIND_PASSWD, YCQL_LDAP_NEW_SECRET);
    requestTserverFlags.put("log_max_seconds_to_retain", "86333");

    // Fail after two processes are configured, so the retry finishes a half-applied upgrade.
    List<String> configuredProcesses = new ArrayList<>();
    localNodeManager.setFailureInjection(
        pair -> {
          if (pair.getFirst() != NodeManager.NodeCommandType.Configure) {
            return false;
          }
          AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) pair.getSecond();
          if (params.type != UpgradeTaskParams.UpgradeTaskType.GFlags) {
            return false;
          }
          if (configuredProcesses.size() > 1) {
            return true;
          }
          configuredProcesses.add(params.nodeName + "_" + params.getProperty("processType"));
          return false;
        });
    Result upgradeResult =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            app,
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universeUUID
                + "/upgrade/gflags",
            user.createAuthToken(),
            body);
    assertOk(upgradeResult);
    UUID upgradeTaskUUID =
        UUID.fromString(Json.parse(contentAsString(upgradeResult)).get("taskUUID").asText());
    TaskInfo upgradeTask = waitForTask(upgradeTaskUUID, universe);
    assertEquals(TaskInfo.State.Failure, upgradeTask.getTaskState());

    localNodeManager.setFailureInjection(null);
    Result retryResult =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            app,
            "POST",
            "/api/customers/" + customer.getUuid() + "/tasks/" + upgradeTaskUUID + "/retry",
            user.createAuthToken(),
            Json.newObject());
    assertOk(retryResult);
    UUID retryTaskUUID =
        UUID.fromString(Json.parse(contentAsString(retryResult)).get("taskUUID").asText());
    TaskInfo retryTask = waitForTask(retryTaskUUID, universe);
    assertEquals(TaskInfo.State.Success, retryTask.getTaskState());

    universe = Universe.getOrBadRequest(universeUUID);
    checkNodes(
        "after retry",
        universe,
        YCQL_LDAP_NEW_SECRET,
        Map.of("log_max_seconds_to_retain", "86333"));
    checkStoredUniverse("after retry", universeUUID, YCQL_LDAP_NEW_SECRET);
    verifyYSQL(universe);

    for (UUID taskUUID : List.of(upgradeTaskUUID, retryTaskUUID)) {
      checkStoredTaskParams(taskUUID);
      // Only the first attempt captures the state transition, so only it writes a task diff.
      checkAuditRow(taskUUID, taskUUID.equals(upgradeTaskUUID));
      checkNoSecret(
          "task details API of " + taskUUID,
          apiGet("/api/customers/" + customer.getUuid() + "/tasks/" + taskUUID + "/details"));
      checkNoSecret(
          "task audit API of " + taskUUID,
          apiGet("/api/customers/" + customer.getUuid() + "/tasks/" + taskUUID + "/audit_info"));
    }
    checkNoSecret("universe API after retry", getUniverseViaApi(universeUUID).toString());
  }

  private void checkNodes(
      String stage, Universe universe, String ycqlLdapSecret, Map<String, String> otherFlags)
      throws IOException {
    for (NodeDetails node : universe.getNodes()) {
      String where = stage + " " + node.nodeName;
      UniverseDefinitionTaskParams.UserIntent userIntent =
          universe.getCluster(node.placementUuid).userIntent;
      Map<String, String> serverConf = readServerConf(node, userIntent, ServerType.TSERVER);
      check(
          ycqlLdapSecret.equals(serverConf.get(YCQL_LDAP_BIND_PASSWD)),
          where + " server.conf ycql_ldap_bind_passwd=" + serverConf.get(YCQL_LDAP_BIND_PASSWD));
      // YBA appends its own records after the user's.
      String hbaOnDisk = StringUtils.defaultString(serverConf.get(GFlagsUtil.YSQL_HBA_CONF_CSV));
      check(
          hbaOnDisk.equals(HBA_CONF_CSV) || hbaOnDisk.startsWith(HBA_CONF_CSV + ","),
          where + " server.conf ysql_hba_conf_csv=" + hbaOnDisk);
      otherFlags.forEach(
          (k, v) ->
              check(v.equals(serverConf.get(k)), where + " server.conf " + serverConf.get(k)));

      String nodeRoot =
          localNodeManager.getNodeRoot(userIntent, localNodeManager.getNodeInfo(node));
      List<Path> hbaFiles;
      try (Stream<Path> paths = Files.walk(Paths.get(nodeRoot))) {
        hbaFiles =
            paths
                .filter(p -> p.getFileName().toString().equals("ysql_hba.conf"))
                .collect(Collectors.toList());
      }
      check(!hbaFiles.isEmpty(), where + " has no ysql_hba.conf");
      for (Path hbaFile : hbaFiles) {
        List<String> lines = Files.readAllLines(hbaFile);
        check(
            lines.contains(HBA_LDAP_RECORD)
                && !String.join("\n", lines).contains(SECRET_REPLACEMENT),
            where + " " + hbaFile + ": " + lines);
      }
    }
  }

  private Map<String, String> readServerConf(
      NodeDetails node, UniverseDefinitionTaskParams.UserIntent userIntent, ServerType serverType)
      throws IOException {
    String file =
        localNodeManager.getNodeGFlagsFile(
            userIntent, serverType, localNodeManager.getNodeInfo(node));
    Map<String, String> flags = new HashMap<>();
    for (String line : Files.readAllLines(Paths.get(file))) {
      // Split at the first '=' only: the HBA value holds more of them.
      int eq = line.indexOf('=');
      if (line.startsWith("--") && eq > 0) {
        flags.put(line.substring(2, eq), line.substring(eq + 1));
      }
    }
    return flags;
  }

  private void checkStoredUniverse(String stage, UUID universeUUID, String ycqlLdapSecret) {
    String raw =
        DB.sqlQuery(
                "SELECT universe_details_json::text AS d FROM universe"
                    + " WHERE universe_uuid = :u")
            .setParameter("u", universeUUID)
            .findOne()
            .getString("d");
    String where = stage + " universe_details_json";
    check(!raw.contains(YSQL_PASSWORD), where + " holds the YSQL password");
    JsonNode storedFlags = tserverSpecificGFlags(Json.parse(raw));
    check(
        ycqlLdapSecret.equals(storedFlags.path(YCQL_LDAP_BIND_PASSWD).asText()),
        where + " ycql_ldap_bind_passwd=" + storedFlags.path(YCQL_LDAP_BIND_PASSWD));
    check(
        HBA_CONF_CSV.equals(storedFlags.path(GFlagsUtil.YSQL_HBA_CONF_CSV).asText()),
        where + " ysql_hba_conf_csv=" + storedFlags.path(GFlagsUtil.YSQL_HBA_CONF_CSV));
  }

  // A retry runs with these params, so a REDACTED gflag here would reach the nodes.
  private void checkStoredTaskParams(UUID taskUUID) {
    List<SqlRow> rows =
        DB.sqlQuery(
                "SELECT uuid, task_type, task_params::text AS p FROM task_info"
                    + " WHERE uuid = :t OR parent_uuid = :t")
            .setParameter("t", taskUUID)
            .findList();
    check(!rows.isEmpty(), "no task_info rows for " + taskUUID);
    for (SqlRow row : rows) {
      String params = row.getString("p");
      if (params == null) {
        continue;
      }
      String label = "task_params of " + row.getString("task_type") + " " + row.getString("uuid");
      check(!params.contains(YSQL_PASSWORD), label + " holds the YSQL password");
      List<String> redactedGFlagPaths = new ArrayList<>();
      findRedactedPaths(Json.parse(params), "$", redactedGFlagPaths);
      redactedGFlagPaths.removeIf(
          p -> !p.contains(YCQL_LDAP_BIND_PASSWD) && !p.contains(GFlagsUtil.YSQL_HBA_CONF_CSV));
      check(
          redactedGFlagPaths.isEmpty(), label + " holds REDACTED gflags at " + redactedGFlagPaths);
    }
  }

  private void checkAuditRow(UUID taskUUID, boolean expectTaskDiff) {
    SqlRow row =
        DB.sqlQuery(
                "SELECT action, payload::text AS payload,"
                    + " additional_details::text AS details, task_diff::text AS diff"
                    + " FROM audit WHERE task_uuid = :t")
            .setParameter("t", taskUUID)
            .findOne();
    if (row == null) {
      check(false, "no audit row for task " + taskUUID);
      return;
    }
    String where = "audit " + row.getString("action") + " of task " + taskUUID;
    for (String column : List.of("payload", "details", "diff")) {
      String value = row.getString(column);
      if (value == null) {
        check(!column.equals("diff") || !expectTaskDiff, where + " has no task_diff");
        continue;
      }
      checkNoSecret(where + " " + column, value);
      check(
          !value.contains("_redactionParseError"),
          where + " " + column + " fell back to the parse-error wrapper");
    }
    String details = row.getString("details");
    if (details == null) {
      check(false, where + " has no additional_details");
      return;
    }
    boolean sawYcqlChange = false;
    for (JsonNode change : Json.parse(details).path("gflags").path("tserver")) {
      if (YCQL_LDAP_BIND_PASSWD.equals(change.path("name").asText())) {
        sawYcqlChange = true;
        check(
            SECRET_REPLACEMENT.equals(change.path("old").asText())
                && SECRET_REPLACEMENT.equals(change.path("new").asText()),
            where + " ycql change " + change);
      }
    }
    check(sawYcqlChange, where + " records no ycql_ldap_bind_passwd change: " + details);
  }

  private JsonNode getUniverseViaApi(UUID universeUUID) {
    return Json.parse(
        apiGet("/api/customers/" + customer.getUuid() + "/universes/" + universeUUID));
  }

  private String apiGet(String url) {
    Result result = FakeApiHelper.doRequestWithAuthToken(app, "GET", url, user.createAuthToken());
    assertOk(result);
    return contentAsString(result);
  }

  private static JsonNode tserverSpecificGFlags(JsonNode universeDetails) {
    return universeDetails
        .get("clusters")
        .get(0)
        .get("userIntent")
        .get("specificGFlags")
        .get("perProcessFlags")
        .get("value")
        .get(ServerType.TSERVER.name());
  }

  private static void findRedactedPaths(JsonNode node, String path, List<String> out) {
    if (node.isObject()) {
      Iterator<Map.Entry<String, JsonNode>> fields = node.fields();
      while (fields.hasNext()) {
        Map.Entry<String, JsonNode> field = fields.next();
        findRedactedPaths(field.getValue(), path + "." + field.getKey(), out);
      }
    } else if (node.isArray()) {
      for (int i = 0; i < node.size(); i++) {
        findRedactedPaths(node.get(i), path + "[" + i + "]", out);
      }
    } else if (node.isTextual() && node.asText().contains(SECRET_REPLACEMENT)) {
      out.add(path);
    }
  }

  private void checkNoSecret(String where, String content) {
    for (String secret : SECRETS) {
      check(!content.contains(secret), where + " holds " + secret);
    }
  }

  private void check(boolean ok, String problem) {
    if (!ok) {
      log.error("Redaction problem: {}", problem);
      problems.add(problem);
    }
  }

  private class SecretLogScanner extends AppenderBase<ILoggingEvent> {
    @Override
    protected void append(ILoggingEvent event) {
      String message = event.getFormattedMessage();
      if (message == null || LOGGERS_ALLOWED_SECRETS.contains(event.getLoggerName())) {
        return;
      }
      for (String secret : SECRETS) {
        if (message.contains(secret)) {
          problems.add(
              event.getLevel()
                  + " log line from "
                  + event.getLoggerName()
                  + " holds "
                  + secret
                  + ": "
                  + StringUtils.abbreviate(message, 1000));
        }
      }
    }
  }
}

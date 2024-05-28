package com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.MapDifference;
import com.google.common.collect.Maps;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.LdapUnivSync.Params;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.LdapUnivSyncFormData.TargetApi;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;

@Slf4j
public class DbLdapSync extends UniverseTaskBase {

  private final YsqlQueryExecutor ysqlQueryExecutor;
  private final YcqlQueryExecutor ycqlQueryExecutor;

  // ycql/ysql create role statement have a default login=false/NOLOGIN property AND no superuser
  // privileges.
  public static final String YSQL_CREATE_LDAP_GROUP = "CREATE ROLE \"%s\";";
  public static final String YSQL_CREATE_LDAP_USER = "CREATE ROLE \"%s\" WITH LOGIN;";
  public static final String YCQL_CREATE_LDAP_GROUP = "CREATE ROLE IF NOT EXISTS \"%s\";";
  public static final String YCQL_CREATE_LDAP_USER =
      "CREATE ROLE IF NOT EXISTS \"%s\" WITH LOGIN = true;";
  public static final String REVOKE_ROLE = "REVOKE \"%s\" FROM \"%s\";";
  public static final String DROP_ROLE = "DROP ROLE IF EXISTS \"%s\";";
  public static final String GRANT_ROLE = "GRANT \"%s\" TO \"%s\";";

  public static final String YSQL_GET_USERS =
      "SELECT r.rolname AS role, ARRAY(SELECT b.rolname FROM pg_catalog.pg_auth_members m JOIN"
          + " pg_catalog.pg_roles b ON (m.roleid = b.oid) WHERE m.member = r.oid) AS member_of FROM"
          + " pg_catalog.pg_roles r WHERE r.rolname !~ '^(pg_|yb_)' AND r.rolsuper = FALSE ORDER BY"
          + " 1";
  public static final String YCQL_GET_USERS =
      "select role, member_of from system_auth.roles where is_superuser = FALSE;";

  @Inject
  public DbLdapSync(
      BaseTaskDependencies baseTaskDependencies,
      YsqlQueryExecutor ysqlQueryExecutor,
      YcqlQueryExecutor ycqlQueryExecutor) {
    super(baseTaskDependencies);
    this.ysqlQueryExecutor = ysqlQueryExecutor;
    this.ycqlQueryExecutor = ycqlQueryExecutor;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  // get the db state to compare with the ldap state and generate queries
  private List<String> queryDbAndComputeDiff(boolean isYsql, boolean enableDetailedLogs) {
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;

    // query db for user-memberOf mapping
    HashMap<String, List<String>> dbUserMemberOf =
        queryDb(
            isYsql,
            ldapUnivSyncFormData.getDbUser(),
            ldapUnivSyncFormData.getDbuserPassword(),
            enableDetailedLogs);
    if (enableDetailedLogs) {
      log.debug("[DB state] user-to-group mapping: {}", dbUserMemberOf);
    }

    List<String> queriesCreate = new ArrayList<>();
    List<String> queriesUsers = new ArrayList<>();

    // Queries are generated based on the difference between the ldap groups and db users
    if (ldapUnivSyncFormData.getCreateGroups()) {
      log.debug("LDAP groups should be created as part of the sync");
      queriesCreate =
          computeQueriesCreateGroups(dbUserMemberOf.keySet(), isYsql, enableDetailedLogs);
    }

    // Queries are generated based on the difference between the db users and the ldap users
    queriesUsers =
        computeQueriesViaDiffUsers(
            dbUserMemberOf, isYsql, ldapUnivSyncFormData.getExcludeUsers(), enableDetailedLogs);

    List<String> queriesAll = new ArrayList<>();
    queriesAll.addAll(queriesCreate);
    queriesAll.addAll(queriesUsers);

    return queriesAll;
  }

  // query the db for users-memberOf mapping
  private HashMap<String, List<String>> queryDb(
      boolean isYsql, String dbUser, String password, boolean enableDetailedLogs) {
    HashMap<String, List<String>> dbUsers = new HashMap<>();
    Universe universe = getUniverse();
    JsonNode respDB;
    if (isYsql) {
      respDB = ysqlQueryExecutor.runUserDbCommands(YSQL_GET_USERS, "", universe);
    } else {
      RunQueryFormData queryFormData = new RunQueryFormData();
      queryFormData.setQuery(YCQL_GET_USERS);
      queryFormData.setTableType(TableType.YQL_TABLE_TYPE);
      respDB = ycqlQueryExecutor.executeQuery(universe, queryFormData, true, dbUser, password);
    }

    if (respDB != null) {
      if (respDB.has("error")) {
        throw new PlatformServiceException(BAD_REQUEST, respDB.get("error").asText());
      }

      if (respDB.has("result") && !respDB.get("result").isEmpty()) {
        ArrayNode result = (ArrayNode) respDB.get("result");
        if (enableDetailedLogs) {
          log.debug("DB users retrieved: {}", result);
        }
        for (JsonNode userGroup : result) {
          String username = userGroup.get("role").asText(null);
          List<String> groups = new ArrayList<>();
          ArrayNode groupsNode = (ArrayNode) userGroup.get("member_of");
          for (JsonNode group : groupsNode) {
            String groupName = group.asText(null);
            groups.add(groupName);
          }
          dbUsers.put(username, groups);
        }
      }
    }
    return dbUsers;
  }

  // generate queries for creating groups
  public List<String> computeQueriesCreateGroups(
      Set<String> dbStateUsers, boolean isYsql, boolean enableDetailedLogs) {
    List<String> queriesCreate = new ArrayList<>();

    // diff: ldapGroups - dbUsers : to create the LDAP groups if the createGroups is true
    List<String> groupsToCreate =
        taskParams().ldapGroups.stream()
            .filter(group -> !dbStateUsers.contains(group))
            .collect(Collectors.toList());
    if (enableDetailedLogs) {
      log.debug("LDAP groups that are to be created on DB: {}", groupsToCreate);
    }

    for (String group : groupsToCreate) {
      queriesCreate.add(
          (isYsql
              ? String.format(YSQL_CREATE_LDAP_GROUP, group)
              : String.format(YCQL_CREATE_LDAP_GROUP, group)));
    }
    return queriesCreate;
  }

  // generate queries based on the difference between the ldapState and the dbState
  public List<String> computeQueriesViaDiffUsers(
      HashMap<String, List<String>> dbUserMemberOf,
      boolean isYsql,
      List<String> excludeUsers,
      boolean enabledDetailedLogs) {
    List<String> queries = new ArrayList<>();

    // diff: dbState - ldapState
    MapDifference<String, List<String>> difference =
        Maps.difference(dbUserMemberOf, taskParams().userToGroup);
    if (enabledDetailedLogs) {
      log.debug("Difference between the LDAP state and the DB state: {}", difference);
      log.debug("User(s) present only on the LDAP: {}", difference.entriesOnlyOnRight());
      log.debug("User(s) present only on DB: {}", difference.entriesOnlyOnLeft());
      log.debug(
          "User(s) present on both the LDAP server and DB but only differs in the groups they"
              + " belong to: {}",
          difference.entriesDiffering());
    }

    // the user is present only on the LDAP
    difference
        .entriesOnlyOnRight()
        .forEach(
            (username, groups) -> {
              queries.addAll(createAndGrantQueries(username, groups, isYsql));
            });

    // user is present on both ldap and db but only differs in the roles the user belongs to
    log.debug(
        "Revoke queries generated for users that are not included in the exclude users list"
            + " provided..");
    difference
        .entriesDiffering()
        .forEach(
            (username, value) -> {
              List<String> oldGroups = value.leftValue();
              List<String> newGroups = value.rightValue();

              // these groups used to be assigned, maybe not anymore.
              queries.addAll(
                  revokeQueries(
                      username,
                      oldGroups.stream()
                          .filter(
                              oldGroup ->
                                  !newGroups.contains(oldGroup)
                                      && !dbUserMemberOf.keySet().stream()
                                          .noneMatch(key -> key.equals(oldGroup)))
                          .collect(Collectors.toList()),
                      isYsql,
                      excludeUsers));

              // these groups have been newly assigned.
              queries.addAll(
                  grantQueries(
                      username,
                      newGroups.stream()
                          .filter(newGroup -> !oldGroups.contains(newGroup))
                          .collect(Collectors.toList()),
                      isYsql));
            });

    // present only on db
    log.debug(
        "Drop queries generated for users that are not LDAP groups and not mentioned in exclude"
            + " users list provided..");
    difference
        .entriesOnlyOnLeft()
        .forEach(
            (username, groups) -> {
              queries.addAll(dropQueries(username, isYsql, excludeUsers));
            });

    return queries;
  }

  // generate queries for creating user roles and granting privileges to those users
  private List<String> createAndGrantQueries(String username, List<String> groups, boolean isYsql) {
    List<String> queries = new ArrayList<>();
    String createRoleQuery =
        isYsql
            ? String.format(YSQL_CREATE_LDAP_USER, username)
            : String.format(YCQL_CREATE_LDAP_USER, username);
    queries.add(createRoleQuery);
    queries.addAll(grantQueries(username, groups, isYsql));

    return queries;
  }

  // generate queries to revoke privileges
  private List<String> revokeQueries(
      String username, List<String> groups, boolean isYsql, List<String> excludeUsers) {
    List<String> queries = new ArrayList<>();
    if (excludeUsers.isEmpty() || !excludeUsers.contains(username)) {
      for (String group : groups) {
        queries.add(String.format(REVOKE_ROLE, group, username));
      }
    }

    return queries;
  }

  // generate queries to grant privileges
  private List<String> grantQueries(String username, List<String> groups, boolean isYsql) {
    List<String> queries = new ArrayList<>();
    for (String group : groups) {
      queries.add(String.format(GRANT_ROLE, group, username));
    }
    return queries;
  }

  // generate queries to revoke privileges and drop a user's role
  private List<String> dropQueries(String username, boolean isYsql, List<String> excludeUsers) {
    List<String> queries = new ArrayList<>();
    if (!taskParams().ldapGroups.contains(username)
        && (excludeUsers.isEmpty() || !excludeUsers.contains(username))) {
      queries.add(String.format(DROP_ROLE, username));
    }
    return queries;
  }

  @Override
  public void run() {
    log.info("Started {} sub-task for uuid={}", getName(), taskParams().getUniverseUUID());
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;
    List<String> queries = new ArrayList<>();
    boolean enableDetailedLogs = confGetter.getGlobalConf(GlobalConfKeys.enableDetailedLogs);

    if (ldapUnivSyncFormData.getTargetApi() == TargetApi.ysql) {
      queries = queryDbAndComputeDiff(true, enableDetailedLogs);
      if (enableDetailedLogs) {
        log.debug("[YSQL] Queries generated: {}", queries);
      }
      ysqlQueryExecutor.runUserDbCommands(StringUtils.join(queries, "\n"), "", getUniverse());
    } else {
      queries = queryDbAndComputeDiff(false, enableDetailedLogs);
      int i = 0;
      for (String query : queries) {
        RunQueryFormData queryFormData = new RunQueryFormData();
        queryFormData.setQuery(query);
        queryFormData.setTableType(TableType.YQL_TABLE_TYPE);
        JsonNode resp =
            ycqlQueryExecutor.executeQuery(
                getUniverse(),
                queryFormData,
                true,
                ldapUnivSyncFormData.getDbUser(),
                ldapUnivSyncFormData.getDbuserPassword());

        if (resp.has("error")) {
          throw new PlatformServiceException(BAD_REQUEST, resp.get("error").asText());
        }
        if (enableDetailedLogs) {
          log.debug("Finished YCQL DB query {}({} of {})", query, ++i, queries.size());
        }
      }
    }
  }
}

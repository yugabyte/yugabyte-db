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
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.LdapUnivSyncFormData.TargetApi;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;

@Slf4j
public class DbLdapSync extends UniverseTaskBase {

  private final YsqlQueryExecutor ysqlQueryExecutor;
  private final YcqlQueryExecutor ycqlQueryExecutor;

  public static final String DEFAULT_POSTGRES_USERNAME = "postgres";
  private static final String[] EXEMPT_YSQL_USERS =
      new String[] {Util.DEFAULT_YSQL_USERNAME, DEFAULT_POSTGRES_USERNAME};
  private static final String[] EXEMPT_YCQL_USERS = new String[] {Util.DEFAULT_YCQL_USERNAME};

  public static final String YCQL_CREATE_ROLE_SUPERUSER =
      "CREATE ROLE IF NOT EXISTS \"%s\" WITH SUPERUSER = true AND LOGIN = true AND PASSWORD ="
          + " '%s';";
  public static final String YSQL_CREATE_ROLE_SUPERUSER =
      "CREATE ROLE \"%s\" WITH SUPERUSER LOGIN PASSWORD '%s';";
  public static final String REVOKE_ROLE = "REVOKE \"%s\" FROM \"%s\";";
  public static final String DROP_ROLE = "DROP ROLE IF EXISTS \"%s\";";
  public static final String YSQL_CREATE_ROLE = "CREATE ROLE \"%s\" WITH LOGIN PASSWORD '%s';";
  public static final String YCQL_CREATE_ROLE =
      "CREATE ROLE IF NOT EXISTS \"%s\" WITH SUPERUSER = false AND LOGIN = true AND PASSWORD ="
          + " '%s';";

  public static final String GRANT_ROLE = "GRANT \"%s\" TO \"%s\";";
  public static final String YSQL_GET_SUPERUSERS =
      "SELECT r.rolname as role FROM pg_catalog.pg_roles r WHERE r.rolname !~ '^(pg_|yb_)' AND"
          + " r.rolsuper='t' ORDER BY 1";
  public static final String YSQL_GET_USERS =
      "SELECT r.rolname as role, ARRAY(SELECT b.rolname FROM pg_catalog.pg_auth_members m JOIN"
          + " pg_catalog.pg_roles b ON (m.roleid = b.oid) WHERE m.member=r.oid) as member_of FROM"
          + " pg_catalog.pg_roles r WHERE r.rolname !~ '^(pg_|yb_)' order by 1";
  public static final String YCQL_GET_SUPERUSERS =
      "select role from system_auth.roles IF is_superuser = true;";
  public static final String YCQL_GET_USERS = "select role, member_of from system_auth.roles;";

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

  // get the db state and compare with the ldap state for queries
  private List<String> queryDbAndComputeDiff(boolean isYsql) {
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;

    // query db for superusers
    List<String> dbStateSuperusers =
        queryDbSuperusers(
            isYsql, ldapUnivSyncFormData.getDbUser(), ldapUnivSyncFormData.getDbuserPassword());

    // query db for user-memberOf state
    HashMap<String, List<String>> dbStateUsers =
        queryDbUsers(
            isYsql, ldapUnivSyncFormData.getDbUser(), ldapUnivSyncFormData.getDbuserPassword());

    List<String> queriesCreate = new ArrayList<>();
    List<String> queriesUsers = new ArrayList<>();

    // Queries are generated based on the difference between the db superusers and the ldap groups
    if (ldapUnivSyncFormData.getCreateGroups()) {
      queriesCreate = computeQueriesCreateGroups(dbStateSuperusers, isYsql);
    }

    // Queries are generated based on the difference between the db users and the ldap users
    queriesUsers =
        computeQueriesViaDiffUsers(
            dbStateUsers,
            dbStateSuperusers,
            isYsql,
            ldapUnivSyncFormData.getExcludeUsers(),
            ldapUnivSyncFormData.getAllowDropSuperuser());

    List<String> queriesAll = new ArrayList<>();
    queriesAll.addAll(queriesCreate);
    queriesAll.addAll(queriesUsers);

    return queriesAll;
  }

  // query the db for superusers/users
  private JsonNode queryDb(boolean isYsql, String dbUser, String password, boolean superuser) {
    Universe universe = getUniverse();
    JsonNode respDB;
    if (isYsql) {
      respDB =
          ysqlQueryExecutor.runUserDbCommands(
              superuser ? YSQL_GET_SUPERUSERS : YSQL_GET_USERS, "", universe);
    } else {
      RunQueryFormData queryFormData = new RunQueryFormData();
      queryFormData.query = superuser ? YCQL_GET_SUPERUSERS : YCQL_GET_USERS;
      queryFormData.tableType = TableType.YQL_TABLE_TYPE;
      respDB = ycqlQueryExecutor.executeQuery(universe, queryFormData, true, dbUser, password);
    }

    if (respDB != null) {
      if (respDB.has("error")) {
        throw new PlatformServiceException(BAD_REQUEST, respDB.get("error").asText());
      }
    }
    return respDB;
  }

  // query db and get a list of superusers
  private List<String> queryDbSuperusers(boolean isYsql, String dbUser, String password) {
    List<String> dbSuperusers = new ArrayList<>();

    JsonNode respDB = queryDb(isYsql, dbUser, password, true);
    ArrayNode result = (ArrayNode) respDB.get("result");
    for (JsonNode userGroup : result) {
      String groupname = userGroup.get("role").asText(null);
      dbSuperusers.add(groupname);
    }
    return dbSuperusers;
  }

  // query db and get user-memberof mapping
  private HashMap<String, List<String>> queryDbUsers(
      boolean isYsql, String dbUser, String password) {
    HashMap<String, List<String>> dbUsers = new HashMap<>();

    JsonNode respDB = queryDb(isYsql, dbUser, password, false);
    if (respDB.has("result")) {
      ArrayNode result = (ArrayNode) respDB.get("result");
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
    return dbUsers;
  }

  // generate queries for creating groups(superusers)
  public List<String> computeQueriesCreateGroups(List<String> dbStateSuperusers, boolean isYsql) {
    List<String> queriesCreate = new ArrayList<>();

    // diff: ldapState - dbState : to create the groups if the createGroups is tru
    List<String> groupsToCreate =
        taskParams().ldapGroups.stream()
            .filter(group -> !dbStateSuperusers.contains(group))
            .collect(Collectors.toList());

    for (String group : groupsToCreate) {
      queriesCreate.add(
          (isYsql
              ? String.format(YSQL_CREATE_ROLE_SUPERUSER, group, UUID.randomUUID())
              : String.format(YCQL_CREATE_ROLE_SUPERUSER, group, UUID.randomUUID())));
    }
    return queriesCreate;
  }

  // generate queries based on the difference between the ldapState and the dbState for
  // user-memberOf mapping
  public List<String> computeQueriesViaDiffUsers(
      HashMap<String, List<String>> dbStateUsers,
      List<String> dbStateSuperusers,
      boolean isYsql,
      List<String> excludeUsers,
      boolean allowDropSuperuser) {
    List<String> queries = new ArrayList<>();

    // diff: dbState - ldapState
    // ex: dbState: user2: {B}; user3: {C,D}; B: {}; C: {}; D:{} and ldapState: user1: {A,B}; user2:
    // {B,C}
    // diff: not equal: only on left={user3=[C,D], B=[], C[], D[]}: only on right={user1=[A,B]}:
    // value differences={user2=([B], [B,C])}
    MapDifference<String, List<String>> difference =
        Maps.difference(dbStateUsers, taskParams().userToGroup);

    // only on right={user1=[A,B]} --> Create user1 and grant C,D to user1 (the user is present only
    // on ldap)
    difference
        .entriesOnlyOnRight()
        .forEach(
            (username, groups) -> {
              queries.addAll(createAndGrantQueries(username, groups, isYsql));
            });

    // value differences={user2=([B], [B,C]) --> grant C to user2 (user is present on both ldap and
    // db but only differs in the roles the user belongs to)
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
                          .filter(oldGroup -> !newGroups.contains(oldGroup))
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

    // only on left={user3=[C,D], B=[], C[], D[]} --> drop user3, D (present only on db)
    difference
        .entriesOnlyOnLeft()
        .forEach(
            (username, groups) -> {
              queries.addAll(
                  dropQueries(
                      username, isYsql, excludeUsers, allowDropSuperuser, dbStateSuperusers));
            });

    return queries;
  }

  // generate queries for creating user roles and granting privileges to those users (exemptions
  // based on exclusion list)
  private List<String> createAndGrantQueries(String username, List<String> groups, boolean isYsql) {
    List<String> queries = new ArrayList<>();

    boolean isExempt =
        (isYsql && Arrays.asList(EXEMPT_YSQL_USERS).contains(username))
            || (!isYsql && Arrays.asList(EXEMPT_YCQL_USERS).contains(username));

    if (!isExempt) {
      String createRoleQuery =
          isYsql
              ? String.format(YSQL_CREATE_ROLE, username, UUID.randomUUID())
              : String.format(YCQL_CREATE_ROLE, username, UUID.randomUUID());
      queries.add(createRoleQuery);
      queries.addAll(grantQueries(username, groups, isYsql));
    }

    return queries;
  }

  // generate queries to revoke privileges (exemptions based on exclusion list)
  private List<String> revokeQueries(
      String username, List<String> groups, boolean isYsql, List<String> excludeUsers) {
    boolean isExempt =
        (isYsql && Arrays.asList(EXEMPT_YSQL_USERS).contains(username))
            || (!isYsql && Arrays.asList(EXEMPT_YCQL_USERS).contains(username));
    List<String> queries = new ArrayList<>();
    if (!isExempt && (excludeUsers.isEmpty() || !excludeUsers.contains(username))) {
      for (String group : groups) {
        queries.add(String.format(REVOKE_ROLE, group, username));
      }
    }

    return queries;
  }

  // generate queries to grant privileges (exemptions based on exclusion list)
  private List<String> grantQueries(String username, List<String> groups, boolean isYsql) {
    boolean isExempt =
        (isYsql && Arrays.asList(EXEMPT_YSQL_USERS).contains(username))
            || (!isYsql && Arrays.asList(EXEMPT_YCQL_USERS).contains(username));
    List<String> queries = new ArrayList<>();
    if (!isExempt) {
      for (String group : groups) {
        queries.add(String.format(GRANT_ROLE, group, username));
      }
    }
    return queries;
  }

  // generate queries to revoke privileges and drop a user's role (exemptions based on exclusion
  // list)
  private List<String> dropQueries(
      String username,
      boolean isYsql,
      List<String> excludeUsers,
      boolean allowDropSuperuser,
      List<String> dbStateSuperusers) {
    List<String> queries = new ArrayList<>();
    boolean isExempt =
        (isYsql && Arrays.asList(EXEMPT_YSQL_USERS).contains(username))
            || (!isYsql && Arrays.asList(EXEMPT_YCQL_USERS).contains(username));

    if (!isExempt && (excludeUsers.isEmpty() || !excludeUsers.contains(username))) {
      if (dbStateSuperusers.contains(username)) {
        if (allowDropSuperuser && !taskParams().ldapGroups.contains(username)) {
          queries.add(String.format(DROP_ROLE, username));
        }
      } else {
        queries.add(String.format(DROP_ROLE, username));
      }
    }
    return queries;
  }

  @Override
  public void run() {
    log.info("Started {} sub-task for uuid={}", getName(), taskParams().getUniverseUUID());
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;
    List<String> queries = new ArrayList<>();

    if (ldapUnivSyncFormData.getTargetApi() == TargetApi.ysql) {
      queries = queryDbAndComputeDiff(true);
      ysqlQueryExecutor.runUserDbCommands(StringUtils.join(queries, "\n"), "", getUniverse());
    } else {
      queries = queryDbAndComputeDiff(false);
      int i = 0;
      for (String query : queries) {
        RunQueryFormData queryFormData = new RunQueryFormData();
        queryFormData.query = query;
        queryFormData.tableType = TableType.YQL_TABLE_TYPE;
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
        log.debug("Finished YCQL DB Comparison query {} of {}", ++i, queries.size());
      }
    }
  }
}

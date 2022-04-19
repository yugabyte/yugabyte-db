//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.ybcli.commands;

import java.util.List;

import com.google.common.net.HostAndPort;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.shell.core.CommandMarker;
import org.springframework.shell.core.annotation.CliAvailabilityIndicator;
import org.springframework.shell.core.annotation.CliCommand;
import org.springframework.shell.core.annotation.CliOption;
import org.springframework.stereotype.Component;
import org.yb.ColumnSchema;
import org.yb.CommonNet.HostPortPB;
import org.yb.Schema;
import org.yb.client.AsyncYBClient;
import org.yb.client.AsyncYBClient.AsyncYBClientBuilder;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.IsLoadBalancedResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.ChangeLoadBalancerStateResponse;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.util.NetUtil;
import org.yb.util.ServerInfo;

@Component
public class YBCliCommands implements CommandMarker {
  public static final Logger LOG = LoggerFactory.getLogger(YBCliCommands.class);

  // State for current database connection.
  private boolean connectedToDatabase = false;
  private String masterAddresses = null;
  protected static YBClient ybClient;

  @CliAvailabilityIndicator({"connect"})
  public boolean isConnectAvailable() {
    // We are always available to connect to a cluster if we are not connected to one already.
    return !connectedToDatabase;
  }

  @CliAvailabilityIndicator({"disconnect"})
  public boolean isDisconnectAvailable() {
    // We can disconnect from a cluster if connected to one already.
    return connectedToDatabase;
  }

  @CliAvailabilityIndicator({"list tablet-servers", "list tablets", "list tables", "list masters",
                             "change_config", "change_blacklist", "leader_step_down",
                             "get_universe_config", "get_load_move_completion",
                             "is_load_balanced", "is_tserver_ready"})
  public boolean isDatabaseOperationAvailable() {
    // We can perform operations on the database once we are connected to one.
    if (connectedToDatabase) {
      return true;
    } else {
      return false;
    }
  }

  @CliCommand(value = "connect", help = "Connect to a database. Only one connection is allowed.")
  public String connect(
      @CliOption(key = { "masters", "m" },
                 mandatory = true,
                 help = "Comma separated list of masters as '<host>:<port>'")
      final String masterAddresses,
      @CliOption(key = { "certFile", "cert"},
                 help = "CA Certificate for SSL connections.")
      final String certFile,
      @CliOption(key = { "clientCertFile", "clientCert"},
                 help = "Client Certificate for mTLS connections.")
      final String clientCertFile,
      @CliOption(key = { "clientKeyFile", "clientKey"},
                 help = "Client Private Key for mTLS connections.")
      final String clientKeyFile) {
    try {
      AsyncYBClientBuilder builder = new AsyncYBClientBuilder(masterAddresses);
      if (certFile != null) {
        builder.sslCertFile(certFile);
      }
      if (clientCertFile != null) {
        if (clientKeyFile == null) {
          return "ClientKey cannot be null when ClientCert is provided.";
        }
        builder.sslClientCertFiles(clientCertFile, clientKeyFile);
      }
      AsyncYBClient asyncClient = builder.build();
      ybClient = new YBClient(asyncClient);
      this.masterAddresses = masterAddresses;
      connectedToDatabase = true;
      return "Connected to database at " + masterAddresses;
    } catch (Exception e) {
      return "Failed to connect to database at " + masterAddresses;
    }
  }

  @CliCommand(value = "disconnect", help = "Connect to a database")
  public String disconnect() {
    String successMsg = "Disconnected from " + masterAddresses;
    try {
      ybClient.shutdown();
      ybClient = null;
      this.masterAddresses = null;
      connectedToDatabase = false;
      return successMsg;
    } catch (Exception e) {
      return "Failed to disconnect from database at " + masterAddresses;
    }
  }

  private String printServerInfo(List<ServerInfo> servers, boolean isMaster, long elapsed) {
    StringBuilder sb = new StringBuilder();
    sb.append("Got " + servers.size() + (isMaster ? " masters " : " tablet servers "));
    if (isMaster) {
      sb.append("[(index) HostName Port UUID IsLeader State]:\n");
    } else {
      sb.append("[(index) HostName Port UUID]:\n");
    }

    int idx = 1;
    for (ServerInfo server : servers) {
      sb.append("    (" + idx + ") " + server.getHost() + " " + server.getPort() +
                " " + server.getUuid() + (isMaster ? " " + server.isLeader() : "") +
                (isMaster ? " " + server.getState() : "") + "\n");
      idx++;
    }
    sb.append("Time taken: " + elapsed + " ms.");
    return sb.toString();
  }

  @CliCommand(value = "check exists", help = "Check that a table exists")
  public String checkTableExists(
      @CliOption(key = { "keyspace", "k" },
                 mandatory = true,
                 help = "keyspace name")
      final String keyspace,
      @CliOption(key = { "name", "n" },
                 help = "table identifier (name)")
      final String tableName,
      @CliOption(key = { "uuid", "u" },
                 help = "table identifier (uuid)")
      final String tableUuid) {
    try {
      if (tableName != null) {
        return Boolean.toString(ybClient.tableExists(keyspace, tableName));
      } else if (tableUuid != null) {
        return Boolean.toString(ybClient.tableExistsByUUID(tableUuid));
      } else {
        return "Please specify an identifier (name or uuid) for the table you wish to check.";
      }
    } catch (Exception e) {
      String identifier = tableName == null ? tableUuid : tableName;
      return "Failed to check if table " + identifier + "exists, error: " + e;
    }
  }

  @CliCommand(value = "list tablet-servers", help = "List all the tablet servers in this database")
  public String listTabletServers() {
    try {
      ListTabletServersResponse resp = ybClient.listTabletServers();
      return printServerInfo(resp.getTabletServersList(), false, resp.getElapsedMillis());
    } catch (Exception e) {
      return "Failed to fetch tserver list from database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "list tables", help = "List all the tables in this database")
  public String listTables() {
    try {
      ListTablesResponse resp = ybClient.getTablesList();
      StringBuilder sb = new StringBuilder();
      sb.append("Got " + resp.getTablesList().size() +
                " tables [(index) keyspace name uuid type]:\n");
      int idx = 1;
      for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : resp.getTableInfoList()) {
        sb.append("\t(" + idx + ") " + table.getNamespace().getName() + " " + table.getName() +
                  " " + table.getId().toStringUtf8() + " " + table.getTableType() + "\n");
        idx++;
      }
      sb.append("Time taken: " + resp.getElapsedMillis() + " ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to fetch tables list from database at " + masterAddresses + ", error: " + e;
    }
  }

  private void printTableInfo(
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo table, StringBuilder sb) {
    sb.append("Keyspace: ");
    sb.append(table.getNamespace().getName());
    sb.append("\n");
    sb.append("Table name: ");
    sb.append(table.getName());
    sb.append("\n");
    sb.append("Table UUID: ");
    sb.append(table.getId().toStringUtf8());
    sb.append("\n");
    sb.append("Table type: ");
    sb.append(table.getTableType());
    sb.append("\n");
  }

  private void printSchemaInfo(GetTableSchemaResponse response, StringBuilder sb) {
    final Schema schema = response.getSchema();
    sb.append("Table has " + Integer.toString(schema.getColumnCount()) + " columns.\n");
    sb.append(String.format("%-15s", "Column Name"));
    sb.append(String.format("%-15s", "Column ID"));
    sb.append(String.format("%-15s", "Type"));
    sb.append(String.format("%-15s", "Nullable?"));
    sb.append(String.format("%-15s", "Is Key?"));
    sb.append(String.format("%-15s\n", "Is Hash Key?"));
    sb.append(new String(new char[15*6]).replace("\0", "-"));
    sb.append("\n");
    for (ColumnSchema columnSchema : schema.getColumns()) {
      sb.append(String.format("%-15s", columnSchema.getName()));
      sb.append(String.format("%-15s", Integer.toString(columnSchema.getId())));
      sb.append(String.format("%-15s", columnSchema.getType().getName()));
      sb.append(String.format("%-15s", columnSchema.isNullable() ? "YES" : "NO"));
      sb.append(String.format("%-15s", columnSchema.isKey() ? "YES" : "NO"));
      sb.append(String.format("%-15s\n", columnSchema.isHashKey() ? "YES" : "NO"));
    }
  }

  @CliCommand(value = "describe table-uuid", help = "Info on a table in this database by uuid")
  public String infoUuidTable(
      @CliOption(key = { "uuid", "u" },
                 mandatory = true,
                 help = "table identifier (uuid)")
      final String uuid) {
    StringBuilder sb = new StringBuilder();
    try {
      ListTablesResponse resp = ybClient.getTablesList();
      for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : resp.getTableInfoList()) {
        if (table.getId().toStringUtf8().equals(uuid)) {
          printTableInfo(table, sb);
          printSchemaInfo(ybClient.getTableSchemaByUUID(uuid), sb);
          sb.append("Time taken: ");
          sb.append(resp.getElapsedMillis());
          sb.append(" ms.");
          return sb.toString();
        }
      }
      sb.append("Table ");
      sb.append(uuid);
      sb.append(" not found.\n");
      sb.append("Time taken: ");
      sb.append(resp.getElapsedMillis());
      sb.append(" ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to fetch table from database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "describe table", help = "Info on a table in this database.")
  public String infoTable(
      @CliOption(key = { "keyspace", "k" },
                 mandatory = true,
                 help = "keyspace name")
      final String keyspace,
      @CliOption(key = { "table", "t" },
                 mandatory = true,
                 help = "table identifier (name)")
      final String tableName) {
    StringBuilder sb = new StringBuilder();
    try {
      ListTablesResponse resp = ybClient.getTablesList();
      for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : resp.getTableInfoList()) {
        if (table.getNamespace().getName().equals(keyspace) && table.getName().equals(tableName)) {
          printTableInfo(table, sb);
          printSchemaInfo(ybClient.getTableSchema(keyspace, tableName), sb);
          sb.append("Time taken: ");
          sb.append(resp.getElapsedMillis());
          sb.append(" ms.");
          return sb.toString();
        }
      }
      sb.append("Table ");
      sb.append(tableName);
      sb.append(" not found.\n");
      sb.append("Time taken: ");
      sb.append(resp.getElapsedMillis());
      sb.append(" ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to fetch table from database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "list masters", help = "List all the masters in this database")
  public String listMasters() {
    try {
      ListMastersResponse resp = ybClient.listMasters();
      if (resp.hasError()) {
        return "List Masters response failed.";
      }
      return printServerInfo(resp.getMasters(), true, resp.getElapsedMillis());
    } catch (Exception e) {
      return "Failed to fetch masters info for database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "leader_step_down", help = "Try to force the current master leader to step down.")
  public String leaderStepDown() {
    try {
      LeaderStepDownResponse resp = ybClient.masterLeaderStepDown();
      if (resp.hasError()) {
        return "Leader step down response failed, error = " + resp.errorMessage();
      } else {
        ybClient.waitForMasterLeader(ybClient.getDefaultAdminOperationTimeoutMs());
      }

      StringBuilder sb = new StringBuilder();
      sb.append("Success.\n");
      sb.append("Time taken: " + resp.getElapsedMillis() + " ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to step down leader from " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "change_config", help = "Change the master's configuration in this database")
  public String changeConfig(
      @CliOption(key = { "master_host", "h", "host" },
                 mandatory = true,
                 help = "IP address of the master") final String host,
      @CliOption(key = { "master_port", "p", "port" },
                 mandatory = true,
                 help = "RPC port of the master") final int port,
      @CliOption(key = { "isAdd" },
                 mandatory = true,
                 help = "True implies master add case, else master removal.") final boolean isAdd) {
    try {
      ChangeConfigResponse resp = ybClient.changeMasterConfig(host, port, isAdd);
      if (resp.hasError()) {
        return "Change Config response failed, error = " + resp.errorMessage();
      }
      StringBuilder sb = new StringBuilder();
      sb.append(resp.hasError() ? "Hit Error: " + resp.errorMessage() + "\n" : "Success.\n");
      sb.append("Time taken: " + resp.getElapsedMillis() + " ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to fetch masters info for database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "change_blacklist",
      help = "Change the set of blacklisted nodes for the universe")
  public String changeBlacklist(
      @CliOption(key = { "servers", "s"},
                 mandatory = true,
                 help = "CSV of host:port pairs for target servers") final String servers,
      @CliOption(key = { "isAdd" },
                 mandatory = true,
                 help = "True implies adding to blacklist, else removing.") final boolean isAdd) {
    try {
      List<HostPortPB> modifyHosts = NetUtil.parseStringsAsPB(servers);

      ModifyMasterClusterConfigBlacklist operation = new ModifyMasterClusterConfigBlacklist(ybClient, modifyHosts, isAdd);

      operation.doCall();

      return "Success.\n";
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }

  @CliCommand(value = "get_universe_config",
              help = "Get the placement info and blacklist info of the universe")
  public String getUniverseConfig() {
    try {
      GetMasterClusterConfigResponse resp = ybClient.getMasterClusterConfig();

      if (resp.hasError()) {
        return "Failed: " + resp.errorMessage();
      }

      return "Config: \n" + resp.getConfig();
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }

  @CliCommand(value = "get_load_move_completion",
              help = "Get the completion percentage of tablet load move from blacklisted servers.")
  public String getLoadMoveCompletion() {
    try {
      GetLoadMovePercentResponse resp = ybClient.getLoadMoveCompletion();

      if (resp.hasError()) {
        return "Failed: " + resp.errorMessage();
      }

      return "Percent completed = " + resp.getPercentCompleted() +
        " : Remaining = " + resp.getRemaining() + " out of Total = " + resp.getTotal();
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }

  @CliCommand(value = "is_server_ready",
              help = "Check if server is ready to serve IO requests.")
  public String getIsTserverReady(
      @CliOption(key = {"host", "h"},
                 mandatory = true,
                 help = "Hostname or IP of the server. ") final String host,
      @CliOption(key = {"port", "p"},
                 mandatory = true,
                 help = "RPC port number of the server.") final int port,
      @CliOption(key = {"isTserver", "t"},
                 mandatory = true,
                 help = "True imples the tserver, else master.") final boolean isTserver) {
    try {
      HostAndPort hp = HostAndPort.fromParts(host, port);
      IsServerReadyResponse resp = ybClient.isServerReady(hp, isTserver);

      if (resp.hasError()) {
        return "Failed: server response error : " + resp.errorMessage();
      }

      return "Server is ready.";
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString();
    }
  }

  @CliCommand(value = "is_load_balanced",
              help = "Check if master leader thinks that the load is balanced across tservers.")
  public String getIsLoadBalanced() {
    try {
      ListTabletServersResponse list_resp = ybClient.listTabletServers();

      if (list_resp.hasError()) {
        return "Failed: Cannot list tablet servers. Error : " + list_resp.errorMessage();
      }

      LOG.info("Checking load across " + list_resp.getTabletServersCount() + " tservers.");
      IsLoadBalancedResponse resp = ybClient.getIsLoadBalanced(list_resp.getTabletServersCount());

      if (resp.hasError()) {
        return "Load is not balanced.";
      }

      return "Load is balanced.";
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }

  @CliCommand(value = "set_load_balancer_enable",
              help = "Set the load balancer state.")
  public String setLoadBalancerState(
     @CliOption(key = { "v", "value" },
                mandatory = true,
                help = "True implies enable, false implies disable.") final boolean isEnable) {
    try {
      ChangeLoadBalancerStateResponse resp = ybClient.changeLoadBalancerState(isEnable);

      if (resp.hasError()) {
        return "Failed: " + resp.errorMessage();
      }

      return "Load Balancer was " + (isEnable ? "enabled." : "disabled.");
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }

  @CliCommand(value = "ping",
              help = "Ping a certain YB server.")
  public String ping(
      @CliOption(key = {"host", "h"},
                 mandatory = true,
                 help = "Hostname or IP of the server. ") final String host,
      @CliOption(key = {"port", "p"},
                 mandatory = true,
                 help = "Port number of the server. ") final int port) {
    try {
      boolean ret = ybClient.ping(host, port);

      return ret ? "Success." : "Failed.";
    } catch (Exception e) {
      LOG.error("Caught exception ", e);
      return "Failed: " + e.toString() + "\n";
    }
  }
}

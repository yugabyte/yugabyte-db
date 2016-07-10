package org.yb.ybcli.commands;

import java.util.ArrayList;
import java.util.List;

import org.springframework.shell.core.CommandMarker;
import org.springframework.shell.core.annotation.CliAvailabilityIndicator;
import org.springframework.shell.core.annotation.CliCommand;
import org.springframework.shell.core.annotation.CliOption;
import org.springframework.stereotype.Component;
import org.yb.Common.HostPortPB;
import org.yb.client.AsyncYBClient;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;
import org.yb.util.ServerInfo;

@Component
public class YBCliCommands implements CommandMarker {

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
                             "change config"})
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
      final String masterAddresses) {
    try {
      AsyncYBClient asyncClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses).build();
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
      sb.append("[(index) HostName Port UUID IsLeader]:\n");
    } else {
      sb.append("[(index) HostName Port UUID]:\n");
    }

    int idx = 1;
    for (ServerInfo server : servers) {
      sb.append("    (" + idx + ") " + server.getHost() + " " + server.getPort() +
                " " + server.getUuid() + (isMaster ? " " + server.isLeader() : "") + "\n");
      idx++;
    }
    sb.append("Time taken: " + elapsed + " ms.");
    return sb.toString();
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
      sb.append("Got " + resp.getTablesList().size() + " tables:\n");
      int idx = 1;
      for (String table : resp.getTablesList()) {
        sb.append("\t(" + idx + ") " + table + "\n");
        idx++;
      }
      sb.append("Time taken: " + resp.getElapsedMillis() + " ms.");
      return sb.toString();
    } catch (Exception e) {
      return "Failed to fetch tables list from database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "list masters", help = "List all the masters in this database")
  public String listMasters() {
    try {
      ListMastersResponse resp = ybClient.listMasters();
      // TODO: Add checks for resp.hasError here and in all cases where it is not performed.
      return printServerInfo(resp.getMasters(), true, resp.getElapsedMillis());
    } catch (Exception e) {
      return "Failed to fetch masters info for database at " + masterAddresses + ", error: " + e;
    }
  }

  @CliCommand(value = "leader_step_down", help = "Try to force the current master leader to step down.")
  public String leaderStepDown() {
    try {
      LeaderStepDownResponse resp = ybClient.masterLeaderStepDown();
      StringBuilder sb = new StringBuilder();
      sb.append(resp.hasError() ? "Hit Error " + resp.errorMessage() + ".\n" : "Success.\n");
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
      ChangeConfigResponse resp = ybClient.ChangeMasterConfig(host, port, isAdd);
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
      // TODO: Log the error call stack.
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
      return "Failed: " + e.toString() + "\n";
    }
  }
}

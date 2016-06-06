package org.yb.ybcli.commands;

import org.springframework.shell.core.CommandMarker;
import org.springframework.shell.core.annotation.CliAvailabilityIndicator;
import org.springframework.shell.core.annotation.CliCommand;
import org.springframework.shell.core.annotation.CliOption;
import org.springframework.stereotype.Component;
import org.yb.client.AsyncYBClient;
import org.yb.client.ListTablesResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;

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

	@CliAvailabilityIndicator({"list tablet-servers", "list tablets", "list tables"})
	public boolean isDatabaseOperationAvailable() {
	  // We can perform operations on the database once we are connected to one.
		if (connectedToDatabase) {
			return true;
		} else {
			return false;
		}
	}

	@CliCommand(value = "connect", help = "Connect to a database")
	public String connect(
		@CliOption(key = { "masters" },
		           mandatory = true,
		           help = "Comma separated list of masters as '<host>:<port>'") final String masterAddresses) {
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

	@CliCommand(value = "list tablet-servers", help = "List all the tablet servers in this database")
	public String listTabletServers() {
	  try {
	    ListTabletServersResponse resp = ybClient.listTabletServers();

	    StringBuilder sb = new StringBuilder();
	    sb.append("Got " + resp.getTabletServersCount() + " tablet servers:\n");
	    int idx = 1;
	    for (String tserver : resp.getTabletServersList()) {
	      sb.append("    (" + idx + ") " + tserver + "\n");
	      idx++;
	    }
	    sb.append("Time taken: " + resp.getElapsedMillis() + " ms.");
      return sb.toString();
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
}

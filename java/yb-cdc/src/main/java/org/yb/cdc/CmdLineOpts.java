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

package org.yb.cdc;

import org.apache.commons.cli.BasicParser;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.ParseException;
import org.apache.log4j.Logger;

public class CmdLineOpts {

  private static final String DEFAULT_MASTER_ADDRS = "127.0.0.1:7100";

  private static final Logger LOG = Logger.getLogger(CmdLineOpts.class);

  public String tableName;
  public String namespaceName;
  public String masterAddrs = DEFAULT_MASTER_ADDRS;
  public String streamId = "";

  public static CmdLineOpts createFromArgs(String[] args) throws Exception {
    Options options = new Options();

    options.addOption("master_addrs", true, "List of YB master ips to contact");
    options.addOption("table_name", true,
            "Table to get change capture from in format <namespace>.<table>");
    options.addOption("stream_id", true,
            "Optional stream ID. Use this if you already have a CDC stream set up on the table");

    // Do the actual arg parsing.
    CommandLineParser parser = new BasicParser();
    CommandLine commandLine = null;

    try {
      commandLine = parser.parse(options, args);
    } catch (ParseException e) {
      LOG.error("Error in args, use the --help option to see usage. Exception:", e);
      System.exit(0);
    }

    CmdLineOpts configuration = new CmdLineOpts();
    configuration.initialize(commandLine);
    return configuration;
  }

  private String getRequiredOptionValue(CommandLine commandLine, String opt) throws Exception {
    if (!commandLine.hasOption(opt)) {
      throw new Exception (String.format("Command requires a %s argument", opt));
    }
    return commandLine.getOptionValue(opt);
  }

  public void initialize(CommandLine commandLine) throws Exception {

    if (commandLine.hasOption("master_addrs")) {
      masterAddrs = commandLine.getOptionValue("master_addrs");
    } else {
      LOG.info("Defaulting master_addrs to 127.0.0.1:7100");
    }

    String[] fullTableName = getRequiredOptionValue(commandLine, "table_name").split("\\.");

    if (fullTableName.length != 2) {
      throw new Exception(String.format("Expected a namespace and table name for --table_name in" +
                                        " format <namespace>.<table>"));
    }

    namespaceName = fullTableName[0];
    tableName = fullTableName[1];

    if (commandLine.hasOption("stream_id")) {
      streamId = commandLine.getOptionValue("stream_id");
    }

  }
}

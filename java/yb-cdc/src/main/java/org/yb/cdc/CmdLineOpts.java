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

import org.apache.commons.cli.*;
import org.apache.commons.configuration.PropertiesConfiguration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.AsyncYBClient;

public class CmdLineOpts {
  private static final String DEFAULT_MASTER_ADDRESS = "127.0.0.1:7100";

  private static final Logger LOG = LoggerFactory.getLogger(CmdLineOpts.class);

  public String tableName;
  public String namespaceName;
  public String masterAddrs = DEFAULT_MASTER_ADDRESS;
  public String streamId = "";
  public boolean enableSnapshot;
  public String sslCertFile;
  public String clientCertFile;
  public String clientKeyFile;
  public int maxTablets = AsyncYBClient.DEFAULT_MAX_TABLETS;
  public boolean bootstrap = false;

  // Config file path to be provided from command line.
  public String configFile = "";

  // This option will not go with the help message. Strictly for internal testing purposes.
  public int pollingInterval = 200;

  String lineSeparator = System.lineSeparator();
  String configFileHelpMessage = "\nCreate a config.properties file with the" +
          " following parameters and " +
          "pass its path in config"
              .concat(lineSeparator)
              .concat("\tnum.io.threads=<number-of-io-threads>")
              .concat(lineSeparator)
              .concat("\tschema.name=<your-schema-name>")
              .concat(lineSeparator)
              .concat("\ttable.name=<your-table-name>")
              .concat(lineSeparator)
              .concat("\tsocket.read.timeout.ms=" +
                      "<socket-read-timeout-in-milliseconds>")
              .concat(lineSeparator)
              .concat("\toperation.timeout.ms=" +
                      "<operation-timeout-in-milliseconds>")
              .concat(lineSeparator)
              .concat("\tadmin.operation.timeout.ms=" +
                      "<admin-operation-timeout-in-milliseconds>")
              .concat(lineSeparator)
              .concat("\tmaster.addrs=<host-and-port-of-master-leader>")
              .concat(lineSeparator)
              .concat("\tstream.id=<stream-id-if-any>" + lineSeparator +
                      "\t (if you want one to be created automatically, " +
                      "leave this empty)")
              .concat(lineSeparator)
              .concat(lineSeparator)
              .concat(lineSeparator)
              .concat("If you will provide stream_id, master_address, table_name " +
                      "from command line, it would overwrite the ones provided via " +
                      "config.properties file")
              .concat(lineSeparator);

  String helpMessage = lineSeparator
      .concat("yb-cdc-connector.jar is a tool to use the CDC Client from console")
      .concat(lineSeparator)
      .concat("Usage: java -jar yb-cdc-connector.jar" +
              " --config_file <path-to-config.properties>")
      .concat(" [<options>]").concat(lineSeparator)
      .concat("Options:").concat(lineSeparator)
      .concat("  --help").concat(lineSeparator)
      .concat("    Show help").concat(lineSeparator)
      .concat("  --config_file").concat(lineSeparator)
      .concat("    Provide a path to custom config file").concat(lineSeparator)
      .concat("  --show_config_help").concat(lineSeparator)
      .concat("    Show the parameters that should be there in custom config.properties file")
      .concat(lineSeparator)
      .concat("  --master_address").concat(lineSeparator)
      .concat("    List of YB Master IPs to contact").concat(lineSeparator)
      .concat("  --table_name").concat(lineSeparator)
      .concat("    Table to capture change from, in format <namespace>.<table> or " +
        "<namespace>.<schema>.<table>")
      .concat(lineSeparator)
      .concat("  --stream_id").concat(lineSeparator)
      .concat("    Optional DB stream ID, provide this if you have" +
              " already a CDC stream setup")
      .concat(" on a table").concat(lineSeparator)
      .concat("  --create_new_db_stream_id").concat(lineSeparator)
      .concat("    Flag to specify that a new DB stream ID is supposed to be created for use")
      .concat(lineSeparator)
      .concat("  --disable_snapshot").concat(lineSeparator)
      .concat("    Flag to specify whether to disable snapshot, default behaviour is to " +
              "take snapshot")
      .concat(lineSeparator)
      .concat("  --ssl_cert_file").concat(lineSeparator)
      .concat("    Path to SSL certificate file")
      .concat(lineSeparator)
      .concat("  --client_cert_file").concat(lineSeparator)
      .concat("    Path to client certificate file")
      .concat(lineSeparator)
      .concat("  --client_key_file").concat(lineSeparator)
      .concat("    Path to client key file")
      .concat(lineSeparator)
      .concat("  --max_tablets").concat(lineSeparator)
      .concat("    Maximum number of tablets the client can poll for, default is 10")
      .concat(lineSeparator)
      .concat("  --bootstrap").concat(lineSeparator)
      .concat("    Whether to bootstrap the table. This flag has no effect if " +
              "--disable_snapshot is not provided i.e. if you are taking a snapshot, " +
              "bootstrapping will be ignored")
      .concat(lineSeparator);

    public static CmdLineOpts createFromArgs(String[] args) throws Exception {
      Options options = new Options();

      options.addOption("master_address", true,
              "List of YB master ips to contact");
      options.addOption("table_name", true,
              "Table to get change capture from in format <namespace>.<table>");
      options.addOption("stream_id", true,
              "Optional stream ID. Use this if you already have a CDC stream set" +
                      " up on the table");

      // The config file can be passed from the command line using this option.
      // A sample config.properties file has been provided under resources/config.properties
      options.addOption("config_file", true,
              "Path to config.properties file");

      // Specify whether to create a new stream ID.
      options.addOption("create_new_db_stream_id", false,
              "Flag to create new cdc db stream id");

      options.addOption("help", false, "Option to display help");
      options.addOption("show_config_help", false,
              "Flag to display example for config.properties");

      options.addOption("disable_snapshot", false,
              "Option to disable snapshot of a table");

      options.addOption("ssl_cert_file", true,
              "Option to provide the cert file if SSL/TLS enabled");

      // Client certificate and key files.
      options.addOption("client_cert_file", true,
              "Option to provide client certificate file");
      options.addOption("client_key_file", true,
              "Option to provide client key file");

      // Maximum number of tablets the client can poll for, default is 10.
      options.addOption("max_tablets", true, "Maximum number of tablets the client can " +
        "poll for");

      // The interval at which the changes should be poplled at.
      options.addOption("polling_interval", true,
        "Interval at which the changes should be polled at");

      options.addOption("bootstrap", false, "Whether to bootstrap the table");

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
        throw new Exception(String.format("Command requires a %s argument", opt));
      }
      return commandLine.getOptionValue(opt);
    }

    public void initialize(CommandLine commandLine) throws Exception {
      // If the help options are there (--help, --show_config_help) then the relevant help
      // message will be displayed and the program will exit from there.
      if (commandLine.hasOption("help")) {
          LOG.info(helpMessage);
          System.exit(0);
      }
      if (commandLine.hasOption("show_config_help")) {
          LOG.info(configFileHelpMessage);
          System.exit(0);
      }

      if (commandLine.hasOption("master_address")) {
          masterAddrs = commandLine.getOptionValue("master_address");
      } else {
          LOG.info("Looking out for the master_address in the cdc config file");
      }

      if (commandLine.hasOption("table_name")) {
        LOG.info("Setting up table name from command line");

        String[] fullTableName = commandLine.getOptionValue("table_name").split("\\.");

        if (fullTableName.length < 2) {
          throw new Exception("Expected a namespace and table name for --table_name in the " +
            "format <namespace>.<table>\nRun with --help for more options");
        } else if (fullTableName.length > 3) {
          throw new Exception("Table name exceeds the expected length, run with --help " +
            "to see options");
        }

        if (fullTableName.length == 2) {
          // This means that no schema name is provided and the format is <namespace>.<table>
          // The first element in this case would be namespace name and second would be table name.

          namespaceName = fullTableName[0];
          tableName = fullTableName[1];
        } else if (fullTableName.length == 3) {
          // This means schema name is provided in format <namespace>.<schema>.<table>
          // The first element is namespace name, second is schema name and third is table name.

          namespaceName = fullTableName[0];
          tableName = fullTableName[1] + "." + fullTableName[2]; // Parsing would be done later.
        }
      } else {
        LOG.info("Looking out for the table name in the cdc config file");
      }

      if (commandLine.hasOption("stream_id")) {
        streamId = commandLine.getOptionValue("stream_id");
      }

      if (commandLine.hasOption("disable_snapshot")) {
        enableSnapshot = false;
      } else {
        enableSnapshot = true;
      }

      if (commandLine.hasOption("ssl_cert_file")) {
        sslCertFile = commandLine.getOptionValue("ssl_cert_file");
      }

      if (commandLine.hasOption("client_cert_file")) {
        clientCertFile = commandLine.getOptionValue("client_cert_file");
      }

      if (commandLine.hasOption("client_key_file")) {
        clientKeyFile = commandLine.getOptionValue("client_key_file");
      }

      if (commandLine.hasOption("max_tablets")) {
        maxTablets = Integer.parseInt(commandLine.getOptionValue("max_tablets"));
      }

      if (commandLine.hasOption("polling_interval")) {
        pollingInterval = Integer.parseInt(commandLine.getOptionValue("polling_interval"));
      }

      if (commandLine.hasOption("bootstrap")) {
        bootstrap = true;
      }

      // Check if a config file has been provided.
      if (commandLine.hasOption("config_file")) {
        LOG.info("Setting up config file path from command line");
        configFile = commandLine.getOptionValue("config_file");

        PropertiesConfiguration propConfig = new PropertiesConfiguration(configFile);
        if (commandLine.hasOption("table_name")) {
            propConfig.setProperty("schema.name", namespaceName);
            propConfig.setProperty("table.name", tableName);
            LOG.info("Setting up table.name and schema.name from command line");
        }
        if (commandLine.hasOption("stream_id") && commandLine
                .hasOption("create_new_db_stream_id")) {
            // This is not allowed as it would result in conflicting behaviour.
            LOG.error("Providing both stream_id and create_new_db_stream_id" +
                    " together is not allowed");
            System.exit(0);
        }
        if (commandLine.hasOption("stream_id")) {
            propConfig.setProperty("stream.id", streamId);
            LOG.info("Setting up stream.id from command line");
        }
        if (commandLine.hasOption("create_new_db_stream_id")) {
            /* If this option is passed via command line then a new db stream id
             * would be created even when the config.properties file contains a stream id
             * the default behaviour in case this option is not passed would be to take whatever
             * there is in the config file, if the stream id is empty then a new one would be
             * created and if there is a pre-existing stream id in the file, it would be used.
             */
            propConfig.setProperty("stream.id", "");
        }
        if (commandLine.hasOption("master_address")) {
            propConfig.setProperty("master.address", masterAddrs);
            LOG.info("Setting up master.addrs from command line");
        }

        propConfig.setProperty("format", "proto");

        propConfig.save();
      } else {
        LOG.error("config_file not specified, use --config_file <path-to-config-file>" +
                " to specify a config file");

        LOG.info(configFileHelpMessage);
        System.exit(0);
      }
    }
}

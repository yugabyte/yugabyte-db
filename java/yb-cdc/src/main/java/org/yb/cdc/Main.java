package org.yb.cdc;

import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PatternLayout;

public class Main {
  private static final Logger LOG = Logger.getLogger(Main.class);

  private LogConnector connector;

  public Main(CmdLineOpts cmdLineOpts) throws Exception {
    connector = new LogConnector(cmdLineOpts);
  }

  public void run() {
    try {
      connector.run();
    } catch (Exception e) {
      LOG.error("Application ran into error: ", e);
      System.exit(0);
    }
  }

  public static void main(String[] args) throws Exception {
    LOG.info("Starting CDC Console Connector...");
    // First remove all appenders.
    Logger.getLogger("org.yb.cdc").removeAppender("YBConsoleLogger");
    Logger.getRootLogger().removeAppender("YBConsoleLogger");;

    // Create the console appender.
    ConsoleAppender console = new ConsoleAppender();
    console.setName("YBConsoleLogger");
    String PATTERN = "%d [%p|%c|%C{1}] %m%n";
    console.setLayout(new PatternLayout(PATTERN));
    console.setThreshold(Level.INFO);
    console.activateOptions();

    Logger.getLogger("org.yb.cdc").addAppender(console);
    Logger.getLogger("org.yb.cdc").setAdditivity(false);

    CmdLineOpts configuration = CmdLineOpts.createFromArgs(args);
    Main main = new Main(configuration);
    main.run();
  }


}

package org.yb.loadtester;

import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PatternLayout;
import org.yb.loadtester.LoadTester.IOPSThread.IOType;
import org.yb.loadtester.common.Configuration;

/**
 * Load tester App.
 */
public class LoadTester {
  private static final Logger LOG = Logger.getLogger(LoadTester.class);

  static {
    // Enable console logging.
    ConsoleAppender console = new ConsoleAppender();
    String PATTERN = "%d [%p|%c|%C{1}] %m%n";
    console.setLayout(new PatternLayout(PATTERN));
    console.setThreshold(Level.INFO);
    console.activateOptions();
    Logger.getRootLogger().addAppender(console);
  }

  public static class IOPSThread extends Thread {
    private static final Logger LOG = Logger.getLogger(IOPSThread.class);

    public static enum IOType {
      Write,
      Read,
    }

    protected int threadIdx;
    protected Workload workload;
    IOType ioType;

    public IOPSThread(int threadIdx, Workload workload, IOType ioType) {
      this.threadIdx = threadIdx;
      this.workload = workload;
      this.ioType = ioType;
    }

    @Override
    public void run() {
      try {
        LOG.info("Starting " + ioType.toString() + " IOPS thread #" + threadIdx);
        while(!workload.hasFinished()) {
          switch (ioType) {
            case Write: workload.workloadWrite(); break;
            case Read: workload.workloadRead(); break;
          }
        }
        LOG.info("IOPS thread #" + threadIdx + " finished");
      } finally {
        workload.terminate();
      }
    }
  }

  Configuration configuration;
  List<IOPSThread> iopsThreads = new ArrayList<IOPSThread>();
  Workload workload;

  public LoadTester(Configuration configuration) {
    this.configuration = configuration;
    this.workload = configuration.getWorkloadInstance();
  }

  public void run() {
    try {
      // Create the table if needed.
      if (!configuration.getReuseExistingTable()) {
        workload.dropTable();
      }
      workload.createTableIfNeeded();

      // Create the reader and writer threads.
      int idx = 0;
      for (; idx < configuration.getNumWriterThreads(); idx++) {
        iopsThreads.add(new IOPSThread(idx, configuration.getWorkloadInstance(), IOType.Write));
      }
      for (; idx < configuration.getNumWriterThreads() + configuration.getNumReaderThreads();
           idx++) {
        iopsThreads.add(new IOPSThread(idx, configuration.getWorkloadInstance(), IOType.Read));
      }

      // Start the reader and writer threads.
      for (IOPSThread iopsThread : iopsThreads) {
        iopsThread.start();
      }

      // Wait for the various threads to exit.
      while (!iopsThreads.isEmpty()) {
        try {
          iopsThreads.get(0).join();
          iopsThreads.remove(0);
        } catch (InterruptedException e) {
          LOG.error("Error waiting for thread join()", e);
        }
      }
    } finally {
      workload.terminate();
    }
  }

  public static void main(String[] args) throws Exception {
    Configuration configuration = Configuration.createFromArgs(args);
    LoadTester lt = new LoadTester(configuration);
    lt.run();
    LOG.info("Load tester finished");
  }
}

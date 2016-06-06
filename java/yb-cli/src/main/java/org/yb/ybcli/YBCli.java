package org.yb.ybcli;

import java.io.IOException;

import org.springframework.shell.Bootstrap;

public class YBCli {

  /**
   * Start up the spring cli.
   * @param args
   * @throws IOException 
   */
  public static void main(String[] args) throws IOException {
    Bootstrap.main(args);
  }
}

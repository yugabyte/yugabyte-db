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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class CDCConsoleSubscriber {
  private static final Logger LOG = LoggerFactory.getLogger(CDCConsoleSubscriber.class);

  private ConcurrentLogConnector connector;

  public CDCConsoleSubscriber(CmdLineOpts cmdLineOpts, OutputClient opClient) throws Exception {
    connector = new ConcurrentLogConnector(cmdLineOpts, opClient);
  }

  public void run() {
    try {
      connector.run();
    } catch (Exception e) {
      e.printStackTrace();
      LOG.error("Application ran into an error: ", e);
      System.exit(0);
    }
  }

  public void close() {
    try {
      connector.close();
    } catch (Exception e) {
      System.exit(0);
    }
  }

  public static void main(String[] args) throws Exception {
    LOG.info("Starting CDC Console Connector...");

    CmdLineOpts configuration = CmdLineOpts.createFromArgs(args);
    try {
      CDCConsoleSubscriber subscriber = new CDCConsoleSubscriber(configuration, new LogClient());
      subscriber.run();
    }
    catch (Exception e) {
      e.printStackTrace();
      System.exit(1);
    }
  }
}

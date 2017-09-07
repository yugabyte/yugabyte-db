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

using Cassandra;
using CommandLine;
using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading;

namespace YB
{
  class StockTicker
  {
    static int readCount = 0;
    static int writeCount = 0;
    readonly ConcurrentDictionary<int, TickerInfo> tickers = new ConcurrentDictionary<int, TickerInfo> ();
    static DBUtil dbUtil;
    Thread [] threads;
    readonly CLIOptions cliOptions;
    readonly String StockTickerTbl = "stock_ticker_1min";
    readonly int NUM_CURRENCIES = 30;

    public static void Main (string [] args)
    {
      CLIOptions options = new CLIOptions ();
      if (Parser.Default.ParseArguments (args, options)) {
        StockTicker st = new StockTicker (options);
        st.Run ();
      }
    }

    public StockTicker (CLIOptions options)
    {
      cliOptions = options;
    }

    void Run()
    {
      dbUtil = new DBUtil (cliOptions.Nodes);

      switch (cliOptions.Command) {
        case "create-table":
          CreateTable (StockTickerTbl);
          break;
        case "drop-table":
          dbUtil.DropTable (StockTickerTbl);
          break;
        case "run":
          if (!dbUtil.CheckTableExists (StockTickerTbl)) {
            throw new Exception ("Stock Ticker Table doesn't exists, run create-table command.");
          }
          Initalize ();
          Array.ForEach (threads, (Thread thread) => thread.Start ());
          Array.ForEach (threads, (Thread thread) => thread.Join ());
          break;
        default:
          throw new Exception ("Invalid Command " + cliOptions.Command);
      }
      dbUtil.Disconnect ();
    }

    private void CreateTable (String TableName)
    {
      Console.WriteLine ("Creating Stock Ticker table {0}", TableName);
      String statementStr = $"CREATE TABLE IF NOT EXISTS {TableName} (identifier_id int, " +
                            "time_stamp timestamp, first float, last float, high float, low float, " +
                            "volume float, vwap float, twap float, trades bigint, currency int, " +
                            "primary key ((identifier_id), time_stamp)) " +
                            "WITH CLUSTERING ORDER BY (time_stamp DESC)";
      dbUtil.ExecuteQuery (statementStr);
    }

    // We cache the prepared statement and using prepared statement yields higher performance
    private PreparedStatement GetInsertStatement ()
    {
      return dbUtil.GetOrAddQuery ($"INSERT INTO {StockTickerTbl} (identifier_id, time_stamp, first, " +
                                   "last, high, low, volume, vwap, twap, trades, currency) " +
                                   "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)").Result;
    }

    // We cache the prepared statement and using prepared statement yields higher performance
    private PreparedStatement GetSelectStatement ()
    {
        return dbUtil.GetOrAddQuery ($"SELECT * FROM {StockTickerTbl} WHERE identifier_id = ? " +
                                     "AND time_stamp > ? AND time_stamp < ?").Result;
    }

    void Initalize ()
    {
      for (int i = 0; i < cliOptions.NumStockSymbols; i++) {
        tickers.TryAdd (i, new TickerInfo (i));
      }

      threads = new Thread [cliOptions.NumWriterThreads + cliOptions.NumReaderThreads];
      int threadIdx = 0;
      for (int i = 0; i < cliOptions.NumWriterThreads; i++) {
        threads.SetValue (new Thread (DoWrite), threadIdx++);
      }
      for (int i = 0; i < cliOptions.NumReaderThreads; i++) {
        threads.SetValue (new Thread (DoRead), threadIdx++);
      }
    }

    public void DoWrite ()
    {
      bool writeDone = false;
      var random = new Random ();
      while (!writeDone) {
        int randomIdx = random.Next (tickers.Count);
        float first = (float) random.NextDouble ();
        float last = (float) random.NextDouble ();
        float high = (float) random.NextDouble ();
        float low = (float) random.NextDouble ();
        float volume = (float) random.NextDouble ();
        float vwap = (float) random.NextDouble ();
        float twap = (float) random.NextDouble ();
        long trades = (long)random.Next ();
        int currency = random.Next (NUM_CURRENCIES);

        if (tickers.TryRemove (randomIdx, out TickerInfo dataSource)) {
          long ts = dataSource.GetDataEmitTs ();
          if (ts == -1) {
            Thread.Sleep (100);
            continue;
          }

          var counter = Interlocked.Increment (ref writeCount);
          if (cliOptions.NumKeysToWrite > 0 && counter >= cliOptions.NumKeysToWrite) break;
          BoundStatement stmt = GetInsertStatement ().Bind (dataSource.GetTickerId (),
                                                            dataSource.GetDataEmitTime (),
                                                            first, last, high, low, volume,
                                                            vwap, twap, trades, currency);
          dbUtil.ExecuteQuery (stmt);
          dataSource.SetLastEmittedTs (ts);
          tickers.TryAdd (randomIdx, dataSource);
        }
      }
    }

    public void DoRead ()
    {
      bool readDone = false;
      var random = new Random ();
      while (!readDone) {
        int randomIdx = random.Next (tickers.Count);
        if (tickers.TryRemove (randomIdx, out TickerInfo dataSource)) {
          if (!dataSource.HasEmittedData ()) {
            Thread.Sleep (100);
            continue;
          }
          var newReadCount = Interlocked.Increment (ref readCount);
          if (cliOptions.NumKeysToRead > 0 && newReadCount >= cliOptions.NumKeysToRead) break;
          BoundStatement stmt = GetSelectStatement ().Bind (dataSource.GetTickerId (),
                                                            dataSource.GetStartTime (),
                                                            dataSource.GetEndTime ());
          if (dbUtil.ExecuteQuery (stmt).GetRows ().FirstOrDefault () == null) {
            Console.WriteLine ("READ Failed Ticker Id {0}, timestamp {1} to {2}",
                               dataSource.GetTickerId (),
                               dataSource.GetStartTime (),
                               dataSource.GetEndTime ());
          }

          tickers.TryAdd (randomIdx, dataSource);
        }
      }
    }
  }
}

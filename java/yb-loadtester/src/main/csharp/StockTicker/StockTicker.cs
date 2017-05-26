// Copyright (c) YugaByte, Inc.

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
          dbUtil.CreateTable (StockTickerTbl);
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

    private PreparedStatement GetInsertStatement ()
    {
      return dbUtil.GetOrAddQuery ($"INSERT INTO {StockTickerTbl} (ticker_id, ts, value) " +
                                   "VALUES (?, ?, ?)").Result;
    }

    private PreparedStatement GetSelectStatement ()
    {
        return dbUtil.GetOrAddQuery ($"SELECT * FROM {StockTickerTbl} WHERE ticker_id = ? " +
                                     "AND ts > ? AND ts < ? LIMIT 1").Result;
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
                                                            "value-" + ts);
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

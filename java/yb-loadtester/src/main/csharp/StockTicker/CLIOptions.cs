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

using CommandLine;
using CommandLine.Text;
using System.Collections.Generic;

namespace YB
{
  class CLIOptions
  {
    [OptionList ("nodes", Separator = ',', Required = true, HelpText = "Yugabyte node ip and port")]
    public List<string> Nodes { get; set; }

    [Option ("command", Required = true, HelpText = "Command Type (run, create-table, drop-table).")]
    public string Command { get; set; }

    [Option ("stock_symbols", DefaultValue = 1000, HelpText = "Number of stock symbols to load.")]
    public int NumStockSymbols { get; set; }

    [Option ("num_keys_to_write", DefaultValue = -1, HelpText = "Number of keys to write.")]
    public int NumKeysToWrite { get; set; }

    [Option ("num_keys_to_read", DefaultValue = -1, HelpText = "Number of keys to reads.")]
    public int NumKeysToRead { get; set; }

    [Option ("num_writer_threads", DefaultValue = 4, HelpText = "Number of writer threads.")]
    public int NumWriterThreads { get; set; }

    [Option ("num_reader_threads", DefaultValue = 4, HelpText = "Number of reader threads.")]
    public int NumReaderThreads { get; set; }


    [Option ("data_emit_rate", DefaultValue = 1000, HelpText = "Data emit rate in milliseconds.")]
    public int DataEmitRate { get; set; }


    [HelpOption]
    public string GetUsage ()
    {
      var help = new HelpText {
        Heading = new HeadingInfo ("C# Stock Ticker App", "1.0"),
        Copyright = new CopyrightInfo ("YugaByte, Inc", 2016),
        AdditionalNewLineAfterOption = true,
        AddDashesToOption = true
      };
      help.AddPreOptionsLine ("Usage: StockTicker --nodes 127.0.0.1:9042");
      help.AddOptions (this);
      return help;
    }
  }
}

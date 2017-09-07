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

using System;

namespace YB
{
  class TickerInfo
  {
    int tickerId;
    readonly long dataEmitRateMs;
    long lastEmittedTs = -1;
    long dataEmitStartTs = -1;
    public static DateTimeOffset dateOrigin =
      new DateTimeOffset (1970, 1, 1, 0, 0, 0, TimeSpan.Zero);

    public TickerInfo (int tickerId, long dataEmitRateMs = 1000)
    {
      this.tickerId = tickerId;
      this.dataEmitRateMs = dataEmitRateMs;
    }

    public int GetTickerId ()
    {
      return tickerId;
    }

    public void SetLastEmittedTs (long ts)
    {
      if (dataEmitStartTs == -1) {
        dataEmitStartTs = ts;
      }
      if (lastEmittedTs < ts) {
        lastEmittedTs = ts;
      }
    }

    public long GetLastEmittedTs ()
    {
      return lastEmittedTs;
    }

    public bool HasEmittedData ()
    {
      return lastEmittedTs > -1;
    }

    public long GetDataEmitTs ()
    {
      long ts = (DateTimeOffset.Now.Ticks - dateOrigin.Ticks) / TimeSpan.TicksPerMillisecond;
      if ((ts - lastEmittedTs) < dataEmitRateMs) {
        return -1;
      }
      if (lastEmittedTs == -1) {
        return ts - (ts % dataEmitRateMs);
      }
      return (lastEmittedTs + dataEmitRateMs);
    }

    public DateTimeOffset GetDataEmitTime()
    {
      return dateOrigin.AddMilliseconds (GetDataEmitTs());
    }

    public DateTimeOffset GetEndTime ()
    {
      // We would just add 1 second for time being until we have >= and <=
      return dateOrigin.AddMilliseconds (GetLastEmittedTs() + dataEmitRateMs);
    }

    public DateTimeOffset GetStartTime ()
    {
      long deltaT = -1 * (60 + new Random ().Next (540)) * dataEmitRateMs;
      return GetEndTime ().AddMilliseconds (deltaT) ;
    }
  }
}

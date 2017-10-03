# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

library(ggplot2)
library(reshape)

source("multiplot.R")
source("si_vec.R")

d.kudu <- read.table(file="/tmp/kudu.tsv", header=T)
d.kudu$system <- as.factor("kudu")
d.kudu <- subset(d.kudu, select = -c(num_layers))


d.hbase <- read.table(file="/tmp/hbase.tsv", header=T)
d.hbase$system <- as.factor("hbase")
d.hbase <- subset(d.hbase, select = -c(num_storefiles))

d <- rbind(d.kudu, d.hbase)


d$insert_rate = c(0, diff(d$inserted)/diff(d$time))
d$scan_rate = c(0, diff(d$scanned)/diff(d$time))
d <- subset(d, select = -c(scanned))

d.melted <- melt(d, id=c("time", "system"))

vlines <- c(
  geom_vline(xintercept=d.kudu[d.kudu$inserted >= 200*1000*1000,][1,]$time, colour="blue"),
  geom_vline(xintercept=d.hbase[d.hbase$inserted >= 200*1000*1000,][1,]$time, colour="red"))

smooth.span <- 1.0/max(d$time)

p.scan_rate <- ggplot(subset(d.melted, variable=="scan_rate")) +
               aes(x=time, y=value, colour=system) +
               geom_line() +
               scale_y_log10(labels=si_vec) +
               labs(title="Scan rate during insert workload\n(log scale)",
                    x=NULL, y="Rows/sec") +
               vlines

p.insert_rate <- ggplot(subset(d.melted, variable=="insert_rate")) +
               aes(x=time, y=value, colour=system) +
               stat_smooth(span=smooth.span) +
               geom_line(alpha=0.4) +
               scale_y_continuous(labels=si_vec) +
               labs(title="Insert rate during insert workload",
                    x="Time (s)", y="Rows/sec") +
               vlines

scan_rate_histo <- ggplot(d, aes(scan_rate,  fill=system)) +
  geom_density(alpha=0.5) +
  scale_x_log10(labels=si_vec) +
  labs(x="Scan rate (rows/sec)")

insert_rate_histo <- ggplot(d, aes(insert_rate, fill=system)) +
  geom_density(alpha=0.5) +
  scale_x_continuous(labels=si_vec) +
  labs(x="Insert rate (rows/sec)")

tryCatch({dev.off()}, error=function(e){})
multiplot(p.scan_rate, p.insert_rate);
dev.new()
multiplot(scan_rate_histo, insert_rate_histo)



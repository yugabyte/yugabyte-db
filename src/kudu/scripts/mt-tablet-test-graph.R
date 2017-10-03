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

# How to invoke:
#  mt-tablet-test-graph.R <tsvfile> <testname>
# This script takes in input a TSV file that contains the timing results
# from running mt-table-test, and parsed out by graph-metrics.py
# The file needs to have the following header:
#  memrowset_kb    updated scanned time    num_rowsets     inserted
# Three png are generated:
#  - Insert rate as data is inserted
#  - Scan rate as data is inserted
#  - Multiple plots, where x is time, and y shows a variety of different
#    progressions like the number of rowsets over time.

library(ggplot2)
library(reshape)
library(Cairo)

newpng<- function(filename = "img.png", width = 400, height = 400) { 
    CairoPNG(filename, width, height) 
} 

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 2) {
  stop("usage: jobs_runtime.R <tsvfile> <testname>")
}
filename = args[1]
testname = args[2]

source("si_vec.R")
newpng(paste(testname, "-1.png", sep = ""))

print(c("Using file ", filename))

d <- read.table(file=filename, header=T)

d$insert_rate = c(0, diff(d$inserted)/diff(d$time))

if (exists("scanned", where=d)) {
  d$scan_rate = c(0, diff(d$scanned)/diff(d$time))
  d <- subset(d, select = -c(scanned))
}

if (!is.null(d$updated)) {
  d$update_rate = c(0, diff(d$updated)/diff(d$time))
  d <- subset(d, select = -c(updated))
}

# Put memrowset usage in bytes
d$memrowset_bytes <- d$memrowset * 1024
d <- subset(d, select = -c(memrowset_kb))

print(ggplot(d, aes(inserted, insert_rate)) +
          geom_point(alpha=0.5) +
          scale_x_continuous(labels=si_vec) +
          scale_y_log10(labels=si_vec))

if (exists("scan_rate", where=d)) {
  newpng(paste(testname, "-2.png", sep = ""))
  print(ggplot(d, aes(inserted, scan_rate)) +
            geom_point(alpha=0.5) +
            scale_x_continuous(labels=si_vec) +
            scale_y_log10(labels=si_vec))
}

newpng(paste(testname, "-3.png", sep = ""))

d <- rename(d, c(
  insert_rate="Insert rate (rows/sec)",
  memrowset="Memstore Memory Usage"))


if (exists("scan_rate", where=d)) {
  d <- rename(d, c(
    scan_rate="Scan int col (rows/sec)"))
}

# set span to 5 seconds worth of data
span = 5.0/max(d$time)

d.melted = melt(d, id="time")
print(qplot(time, value, data=d.melted, geom="line", group = variable)
                  + scale_y_continuous(labels=si_vec)
                  + facet_grid(variable~., scale = "free_y")
                  + stat_smooth())


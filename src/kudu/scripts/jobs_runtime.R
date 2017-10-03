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
#  jobs_runtime.R <tsvfile> <testname>
# This script takes in input a TSV file with the following header:
# workload        runtime     build_number
# It generates a png where x is the build number, y is the runtime
# and each workload is a different line. The test name is used to generate
# the output file's name.
# R needs to be installed with the graphic libraries

library(Cairo)
library(ggplot2)

newpng <- function(filename = "img.png", width = 1500, height = 500) {
    CairoPNG(filename, width, height)
}

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 2) {
  stop("usage: jobs_runtime.R <filename> <testname>")
}
filename = args[1]
testname = args[2]

newpng(paste(testname, "-jobs-runtime.png", sep = ""))

d <- read.table(file=filename, header=T)

print(ggplot(d, aes(x = build_number, y = runtime, color = workload)) +
             stat_summary(aes(group = workload), fun.y=median, geom = "line") +
             geom_boxplot(aes(group = interaction(workload, build_number)), position = "identity", outlier.size = 1.7, outlier.colour = "gray32") +
             ggtitle(testname))

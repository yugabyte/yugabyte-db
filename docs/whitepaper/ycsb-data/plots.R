library(ggplot2)
library(scales)

data <- read.table(file="data.tsv",header=T)
systems <- levels(data$sys)
workloads <- levels(data$workload)

for (w in workloads) {
  cat("iterating for workload ", w, "\n")
  s <- subset(data, workload==w)
  dists <- unique(s$dist)
  for (d in dists) {
    s2 <- subset(s, dist==d)
    cat("Plotting", nrow(s2), "points for workload", w, "dist", d, "\n")
    filename = paste(d, "-", w, ".png", sep="")
    cat("into filename", filename, "\n")
    png(filename)
    print(qplot(time, tput, data=s2, colour=sys,
        main=paste("Workload '", w, "'\n", d, " distribution", sep=""),
        geom="line", xlab="Time (sec)", ylab="Throughput\n(ops/sec)") +
        scale_y_continuous(labels=comma) +
        theme(legend.position="bottom"))
    dev.off()
  }
}

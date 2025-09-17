#ifdef YB_GOOGLE_TCMALLOC
#include "yb/util/tcmalloc_profile.h"
#include "yb/util/tcmalloc_util.h"

static std::string getTCMallocStatsToPrintOnCron() {
  auto heap_snapshot_result = GetAggregateAndSortHeapSnapshot(
    yb::SampleOrder::kEstimatedBytes,
    yb::HeapSnapshotType::kCurrentHeap,
    yb::SampleFilter::kAllSamples,
    "\n");

  std::string output = "";

  int64_t total_estimated_bytes = 0;

  if (!heap_snapshot_result.ok()) {
    output += "Failed to get heap snapshot: ";
    output += heap_snapshot_result.status().ToString();
    output += "\n";
    return output;
  }

  const std::vector<yb::Sample> samples = heap_snapshot_result.get();

  // This output is consisten with the tserver tcmalloc output yb::Format.
  output += yb::Format("Dumping all the stacks from heap snapshot. Actual heap usage (excluding "
    "TCMalloc internals): $0", yb::GetTCMallocCurrentAllocatedBytes());
  output += yb::Format("\n");
  for (const auto &sample : samples) {
    output += yb::Format(
        "Estimated bytes: $0 ($1KB) , estimated count: $2, sampled bytes: $3, sampled count: $4, stack: $5",
        std::to_string(sample.second.estimated_bytes.value_or(0)),
        std::to_string(sample.second.estimated_bytes.value_or(0)/1024.0),
        std::to_string(sample.second.estimated_count.value_or(0)),
        std::to_string(sample.second.sampled_allocated_bytes),
        std::to_string(sample.second.sampled_count),
        sample.first);
    if (sample.second.estimated_bytes.value_or(0)) {
      int64_t estimated_bytes = sample.second.estimated_bytes.value_or(0);
      total_estimated_bytes += estimated_bytes;
    }
    output += yb::Format("\n");
  }

  output += yb::Format("Total estimated bytes: $0 ($1KB)",
    total_estimated_bytes, std::to_string(total_estimated_bytes/1024.0));
  output += yb::Format("\n");
  return output;

  output += "Start printing the stats for current iteration in a CRON THREAD \n\n";

  for (const auto& sample : samples) {
      output += "Sample: ";
      output += sample.first;
      output += "sampled_allocated_bytes: ";
      output += std::to_string(sample.second.sampled_allocated_bytes);
      output += "\n";
      output += "sampled_count: ";
      output += std::to_string(sample.second.sampled_count);
      output += "\n";
      output += "estimated_count: ";
      output += std::to_string(sample.second.estimated_count.value_or(0));
      output += "\n";
      if (sample.second.estimated_bytes.value_or(0)) {
        int64_t estimated_bytes = sample.second.estimated_bytes.value_or(0);
        total_estimated_bytes += estimated_bytes;
        long double est_kb = estimated_bytes/1024.0;
        long double est_mb = est_kb/1024.0;
        output += "estimated_bytes: ";
        output += std::to_string(est_kb);
        output += "KB (";
        output += std::to_string(est_mb);
        output += "MB)";
        output += "\n\n";
      }
  }

  output += "Total estimated bytes: ";
  output += std::to_string(total_estimated_bytes);
  output += "KB\n";

  output += "End of printing the stacks for current iteration IN A CRON THREAD \n\n";

  return output;
}
#endif // YB_GOOGLE_TCMALLOC

extern "C" {
int odyssey_main(int argc, char *argv[]);
#ifdef YB_GOOGLE_TCMALLOC
char *getTCMallocStats()
{
  std::string s2 = getTCMallocStatsToPrintOnCron();
  char *c2 = (char *)malloc(s2.size() + 1);
  strcpy(c2, s2.c_str());
  return c2;
}

void setTCMallocSamplePeriod(uint64_t sample_period_bytes)
{
  tcmalloc::MallocExtension::SetProfileSamplingRate(sample_period_bytes);
}
#endif // YB_GOOGLE_TCMALLOC
}

int main(int argc, char *argv[]) {
    return odyssey_main(argc, argv);
}

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SystemLogsComponent implements SupportBundleComponent {
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  SystemLogsComponent(
      NodeUniverseManager nodeUniverseManager, SupportBundleUtil supportBundleUtil) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.supportBundleUtil = supportBundleUtil;
  }

  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    // pass
  }

  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> res =
        getFilesListWithSizes(customer, null, universe, startDate, endDate, node);
    // Nothing to download.
    if (res.isEmpty()) {
      return;
    }
    Path nodeTargetFile = Paths.get(bundlePath.toString(), "syslogs" + ".tar.gz");
    nodeUniverseManager.downloadRootFiles(
        customer.getUuid(), node, universe, res.keySet(), nodeTargetFile.toString());

    // Since we download files from the tmp dir of the db node,
    // after file download the file tree is like
    // yb-support-bundle-asharma-aws-20250207072522.781-logs
    //  '-- yb-admin-asharma-aws-n1
    //     '-- tmp
    //        '-- root_files.tar.gz
    // We fix this below and move the files to the correct dir.
    try {
      if (Files.exists(nodeTargetFile)) {
        File unZippedFile =
            FileUtils.unGzip(
                nodeTargetFile.toAbsolutePath().toFile(), bundlePath.toAbsolutePath().toFile());
        Files.delete(nodeTargetFile);
        FileUtils.unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
        unZippedFile.delete();
        String tmpDir = GFlagsUtil.getCustomTmpDirectory(node, universe);
        Path rootFileTar = Paths.get(bundlePath.toString(), tmpDir, "root_files.tar.gz");
        if (Files.exists(rootFileTar)) {
          unZippedFile =
              FileUtils.unGzip(
                  rootFileTar.toAbsolutePath().toFile(), bundlePath.toAbsolutePath().toFile());
          FileUtils.unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
          unZippedFile.delete();
          Path tmpRoot = Paths.get(tmpDir).getName(0);
          FileUtils.deleteDirectory(Paths.get(bundlePath.toString(), tmpRoot.toString()).toFile());
        }
      }
    } catch (Exception e) {
      log.error("Error while collecting system logs: ", e);
      e.printStackTrace();
    }
  }

  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {

    Map<String, Long> res = new HashMap<>();
    String logDir = "/var/log";
    Map<String, Long> fileMap =
        nodeUniverseManager.getNodeFilePathAndSizes(node, universe, logDir, 1, "f");
    for (Entry<String, Long> entry : fileMap.entrySet()) {
      String fileName = entry.getKey();
      // We will only collect files of the form /var/log/messages-yyyymmdd
      if (!fileName.contains("messages")) {
        continue;
      }

      if (fileName.contains("-")) {
        String[] split = fileName.split("-");
        SimpleDateFormat formatter = new SimpleDateFormat("yyyyMMdd");
        try {
          Date fileEndDate = formatter.parse(split[1]);
          // Logs are rotated weekly.
          Date fileStartDate = supportBundleUtil.getDateNDaysAgo(endDate, 7);
          if (supportBundleUtil.checkDateBetweenDates(startDate, fileStartDate, fileEndDate)
              || supportBundleUtil.checkDateBetweenDates(endDate, fileStartDate, fileEndDate)) {
            res.put(fileName, entry.getValue());
          }
        } catch (ParseException e) {
          log.warn(
              "Could not parse date from file {}. Skipping collection in support bundle.",
              fileName);
        }
      } else {
        // Always collect the latest system logs.
        res.put(fileName, entry.getValue());
      }
    }
    return res;
  }
}

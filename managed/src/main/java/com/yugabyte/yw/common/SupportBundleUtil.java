package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.InstanceType;
import java.util.List;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Collections;
import java.util.Date;
import java.util.UUID;
import java.io.File;
import java.nio.file.Path;
import java.text.SimpleDateFormat;
import java.text.ParseException;
import java.text.DateFormat;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.joda.time.DateTime;

@Slf4j
@Singleton
public class SupportBundleUtil {

  public Date getDateNDaysAgo(Date currDate, int days) {
    Date dateNDaysAgo = new DateTime(currDate).minusDays(days).toDate();
    return dateNDaysAgo;
  }

  public Date getDateNDaysAfter(Date currDate, int days) {
    Date dateNDaysAgo = new DateTime(currDate).plusDays(days).toDate();
    return dateNDaysAgo;
  }

  public Date getTodaysDate() throws ParseException {
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
    Date dateToday = sdf.parse(sdf.format(new Date()));
    return dateToday;
  }

  public Date getDateFromBundleFileName(String fileName) throws ParseException {
    SimpleDateFormat bundleSdf = new SimpleDateFormat("yyyyMMddHHmmss.SSS");
    SimpleDateFormat newSdf = new SimpleDateFormat("yyyy-MM-dd");

    String[] fileNameSplit = fileName.split("-");
    String fileDateStr = fileNameSplit[fileNameSplit.length - 2];

    return newSdf.parse(newSdf.format(bundleSdf.parse(fileDateStr)));
  }

  public boolean isValidDate(Date date) {
    return date != null;
  }

  // Checks if a given date is between 2 other given dates (startDate and endDate both inclusive)
  public boolean checkDateBetweenDates(Date dateToCheck, Date startDate, Date endDate) {
    return !dateToCheck.before(startDate) && !dateToCheck.after(endDate);
  }

  public List<String> sortDatesWithPattern(List<String> datesList, String sdfPattern) {
    // Sort the list of dates based on the given 'SimpleDateFormat' pattern
    List<String> sortedList = new ArrayList<String>(datesList);
    Collections.sort(
        sortedList,
        new Comparator<String>() {
          DateFormat f = new SimpleDateFormat(sdfPattern);

          @Override
          public int compare(String o1, String o2) {
            try {
              return f.parse(o1).compareTo(f.parse(o2));
            } catch (ParseException e) {
              return 0;
            }
          }
        });

    return sortedList;
  }

  public List<String> filterList(List<String> list, String regex) {
    // Filter and return only the strings which match a given regex pattern
    List<String> result = new ArrayList<String>();
    for (String entry : list) {
      if (entry.matches(regex)) {
        result.add(entry);
      }
    }
    return result;
  }

  // Gets the path to "yb-data/" folder on the node (Ex: "/mnt/d0", "/mnt/disk0")
  public String getDataDirPath(
      Universe universe, NodeDetails node, NodeUniverseManager nodeUniverseManager, Config config) {
    String dataDirPath = "";
    UserIntent userIntent = universe.getCluster(node.placementUuid).userIntent;
    CloudType cloudType = userIntent.providerType;

    if (cloudType == CloudType.onprem) {
      // On prem universes:
      // Onprem universes have to specify the mount points for the volumes at the time of provider
      // creation itself.
      // This is stored at universe.cluster.userIntent.deviceInfo.mountPoints
      try {
        String mountPoints = userIntent.deviceInfo.mountPoints;
        dataDirPath = mountPoints.split(",")[0];
      } catch (Exception e) {
        String defaultMountPath =
            config.getString("yb.support_bundle.default_mount_point_prefix") + "0";
        log.error(
            String.format("On prem invalid mount points. Defaulting to %s", defaultMountPath), e);
        return defaultMountPath;
      }
    } else if (cloudType == CloudType.kubernetes) {
      // Kubernetes universes:
      // K8s universes have a default mount path "/mnt/diskX" with X = {0, 1, 2...} based on number
      // of volumes
      // This is specified in the charts repo:
      // https://github.com/yugabyte/charts/blob/master/stable/yugabyte/templates/service.yaml
      String mountPoint = config.getString("yb.support_bundle.k8s_mount_point_prefix");
      dataDirPath = mountPoint + "0";
    } else {
      // Other provider based universes:
      // Providers like GCP, AWS have the mountPath stored in the instance types for the most part.
      // Some instance types don't have mountPath initialized. In such cases, we default to
      // "/mnt/d0"
      try {
        String nodeInstanceType = node.cloudInfo.instance_type;
        String providerUUID = userIntent.provider;
        InstanceType instanceType =
            InstanceType.getOrBadRequest(UUID.fromString(providerUUID), nodeInstanceType);
        dataDirPath = instanceType.instanceTypeDetails.volumeDetailsList.get(0).mountPath;
      } catch (Exception e) {
        String defaultMountPath =
            config.getString("yb.support_bundle.default_mount_point_prefix") + "0";
        log.error(
            String.format("Could not get mount points. Defaulting to %s", defaultMountPath), e);
        return defaultMountPath;
      }
    }
    return dataDirPath;
  }

  public void deleteFile(Path filePath) {
    if (FileUtils.deleteQuietly(new File(filePath.toString()))) {
      log.info("Successfully deleted file with path: " + filePath.toString());
    } else {
      log.info("Failed to delete file with path: " + filePath.toString());
    }
  }
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.WireProtocol;
import org.yb.client.YBClient;

@Singleton
public class AutoFlagUtil {

  private final YBClientService ybClientService;
  private final GFlagsValidation gFlagsValidation;

  // Info about auto flags class can be found here
  // https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md
  public static int LOCAL_VOLATILE_AUTO_FLAG_CLASS = 1;
  public static String LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME = "kLocalVolatile";

  public static int LOCAL_PERSISTED_AUTO_FLAG_CLASS = 2;
  public static String LOCAL_PERSISTED_AUTO_FLAG_CLASS_NAME = "kLocalPersisted";

  public static int EXTERNAL_AUTO_FLAG_CLASS = 3;
  public static String EXTERNAL_AUTO_FLAG_CLASS_NAME = "kExternal";

  public static final Logger LOG = LoggerFactory.getLogger(AutoFlagUtil.class);

  @Inject
  public AutoFlagUtil(GFlagsValidation gFlagsValidation, YBClientService ybClientService) {
    this.gFlagsValidation = gFlagsValidation;
    this.ybClientService = ybClientService;
  }

  /**
   * Gets auto flag config of a universe.
   *
   * @param universe
   * @return autoFlagConfig
   */
  public WireProtocol.AutoFlagsConfigPB getAutoFlagConfigForUniverse(Universe universe) {
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterAddresses, certificate)) {
      return client.autoFlagsConfig().getAutoFlagsConfig();
    } catch (Exception e) {
      LOG.error(
          "Error occurred while fetching auto flags config for universe " + universe + ": ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Returns set of string containing promoted and modified auto flags having class higher than
   * skipLowerAutoFlagClass for a server(master/tserver).
   *
   * @param universe
   * @param serverType
   * @param skipLowerAutoFlagClass
   * @return
   * @throws IOException
   */
  public Set<String> getPromotedAutoFlags(
      Universe universe, UniverseTaskBase.ServerType serverType, int skipLowerAutoFlagClass)
      throws IOException {
    // Fetch promoted auto flags list from DB itself.
    WireProtocol.AutoFlagsConfigPB autoFlagsConfigPB = getAutoFlagConfigForUniverse(universe);
    Set<String> autoFlags =
        autoFlagsConfigPB.getPromotedFlagsList().stream()
            .filter(
                promotedFlagsPerProcessPB -> {
                  return promotedFlagsPerProcessPB
                      .getProcessName()
                      .equals(
                          UniverseTaskBase.ServerType.MASTER.equals(serverType)
                              ? "yb-master"
                              : "yb-tserver");
                })
            .findFirst()
            .get()
            .getFlagsList()
            .stream()
            .collect(Collectors.toSet());

    // Add auto flags which are modified through gflags override in the promoted auto flags list.
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Map<String, GFlagsValidation.AutoFlagDetails> autoFlagDetails =
        gFlagsValidation.extractAutoFlags(version, serverType).autoFlagDetails.stream()
            .collect(Collectors.toMap(flag -> flag.name, Function.identity()));
    for (Map.Entry<String, String> entry :
        GFlagsUtil.getBaseGFlags(
                serverType,
                universe.getUniverseDetails().getPrimaryCluster(),
                universe.getUniverseDetails().clusters)
            .entrySet()) {
      if (autoFlagDetails.containsKey(entry.getKey())) {
        autoFlags.add(entry.getKey());
      }
    }

    // Remove auto flags having class lower or equal to skipLowerAutoFlagClass
    Iterator<String> itr = autoFlags.iterator();
    while (itr.hasNext()) {
      String autoFlag = itr.next();
      if (autoFlagDetails.containsKey(autoFlag)
          && autoFlagDetails.get(autoFlag).flagClass <= skipLowerAutoFlagClass) {
        itr.remove();
      }
    }
    return autoFlags;
  }
}

// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.CanaryPauseState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Persists software upgrade AZ-level progress to {@code prevYBSoftwareConfig}. When {@code
 * pauseAfter} is true (canary pause point), also sets {@code softwareUpgradeState} to Paused.
 * Otherwise only updates progress fields and leaves upgrade state unchanged.
 */
@Slf4j
public class SaveSoftwareUpgradeProgress extends UniverseTaskBase {

  @Inject
  protected SaveSoftwareUpgradeProgress(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public boolean isCanaryUpgrade;
    public CanaryPauseState canaryPauseState = null;
    public List<AZUpgradeState> masterAZUpgradeStatesList = new ArrayList<>();
    public List<AZUpgradeState> tserverAZUpgradeStatesList = new ArrayList<>();

    /** When true, set universe {@code softwareUpgradeState} to Paused after saving progress. */
    public boolean pauseAfter;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    Params p = taskParams();
    UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          if (!details.updateInProgress) {
            throw new RuntimeException(
                "Universe " + p.getUniverseUUID() + " is not being updated.");
          }
          PrevYBSoftwareConfig prev = details.prevYBSoftwareConfig;
          if (prev == null) {
            prev = new PrevYBSoftwareConfig();
            details.prevYBSoftwareConfig = prev;
          }
          prev.setCanaryUpgrade(p.isCanaryUpgrade);
          prev.setCanaryPauseState(p.canaryPauseState);
          prev.setMasterAZUpgradeStatesList(
              p.masterAZUpgradeStatesList != null
                  ? new ArrayList<>(p.masterAZUpgradeStatesList)
                  : new ArrayList<>());
          prev.setTserverAZUpgradeStatesList(
              p.tserverAZUpgradeStatesList != null
                  ? new ArrayList<>(p.tserverAZUpgradeStatesList)
                  : new ArrayList<>());
          if (p.pauseAfter) {
            details.softwareUpgradeState = UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
          }
          universe.setUniverseDetails(details);
        };
    saveUniverseDetails(updater);
    log.info("Software upgrade progress saved for universe {}.", p.getUniverseUUID());
  }
}

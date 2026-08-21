import { useEffect } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { useDispatch } from 'react-redux';

import { patchTasksForCustomer } from '@app/actions/tasks';
import { TASK_SHORT_TIMEOUT } from '@app/components/tasks/constants';
import { PollingIntervalMs } from '@app/components/xcluster/constants';
import { YBModal, YBModalProps } from '@app/redesign/components';
import { TaskState } from '@app/redesign/features/tasks/dtos';
import { getLatestSoftwareUpgradeTaskForUniverse } from '@app/redesign/features/tasks/TaskUtils';
import {
  api,
  dbUpgradeMetadataQueryKey,
  taskQueryKey,
  universeQueryKey
} from '@app/redesign/helpers/api';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { getUniverse, precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { DbUpgradeProgressPanel } from './DbUpgradeProgressPanel';

interface DbUpgradeManagementSidePanelProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

const useStyles = makeStyles((theme) => ({
  descriptionContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1.5),

    padding: theme.spacing(2),

    border: '1px solid',
    borderColor: theme.palette.divider,
    borderRadius: theme.shape.borderRadius
  },
  descriptionTitle: {
    color: theme.palette.grey[600],
    fontSize: 11.5,
    fontWeight: 500,
    lineHeight: '16px'
  },
  descriptionContent: {
    display: 'flex',
    gap: theme.spacing(1)
  },
  progressPanel: {
    marginTop: theme.spacing(2)
  }
}));

export const DbUpgradeManagementSidePanel = ({
  universeUuid,
  modalProps
}: DbUpgradeManagementSidePanelProps) => {
  const classes = useStyles();
  const dispatch = useDispatch();
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.dbUpgradeManagementSidePanel'
  });

  const isPanelOpen = !!modalProps.open;

  // Universe-scoped task list (same as UniverseTaskList): drives panel + Redux banner sync.
  const universeTasksQuery = useQuery(
    taskQueryKey.universe(universeUuid),
    () => api.fetchCustomerTasks(universeUuid),
    {
      enabled: isPanelOpen && !!universeUuid,
      refetchInterval: TASK_SHORT_TIMEOUT,
      onSuccess(data) {
        dispatch(patchTasksForCustomer(universeUuid, data));
      }
    }
  );

  const latestSoftwareUpgradeTask = getLatestSoftwareUpgradeTaskForUniverse(
    universeTasksQuery.data,
    universeUuid
  );

  const targetDbVersion = latestSoftwareUpgradeTask?.details?.versionNumbers?.ybSoftwareVersion;
  const dbUpgradeMetadataQuery = useQuery(
    dbUpgradeMetadataQueryKey.detail(universeUuid, {
      yb_software_version: targetDbVersion ?? ''
    }),
    () =>
      precheckSoftwareUpgrade(universeUuid, {
        yb_software_version: targetDbVersion ?? ''
      }),
    {
      enabled: isPanelOpen && !!targetDbVersion
    }
  );

  const universeDetailsQuery = useQuery(
    universeQueryKey.detailsV2(universeUuid),
    () => getUniverse(universeUuid),
    { enabled: isPanelOpen && !!universeUuid, refetchInterval: PollingIntervalMs.FOCUSED_TASK }
  );

  const universe = universeDetailsQuery.data;
  const softwareUpgradeState = universe?.info?.software_upgrade_state;
  const isDbUpgradeFinalizationRequired =
    (dbUpgradeMetadataQuery.data?.finalize_required ||
      softwareUpgradeState === UniverseInfoSoftwareUpgradeState.PreFinalize) ??
    false;
  const isYsqlMajorUpgrade = dbUpgradeMetadataQuery.data?.ysql_major_version_upgrade ?? false;
  const latestSoftwareUpgradeTaskStatus = latestSoftwareUpgradeTask?.status;

  // Auto-close once the upgrade has fully settled (universe back in `Ready`) and
  // the task succeeded.
  useEffect(() => {
    if (!isPanelOpen) return;
    if (softwareUpgradeState !== UniverseInfoSoftwareUpgradeState.Ready) return;
    if (latestSoftwareUpgradeTaskStatus !== TaskState.SUCCESS) return;
    modalProps.onClose();
  }, [isPanelOpen, softwareUpgradeState, latestSoftwareUpgradeTaskStatus, modalProps.onClose]);

  return (
    <YBModal
      title={t('title')}
      overrideWidth="730px"
      enableBackdropDismiss
      isSidePanel
      {...modalProps}
    >
      <div className={classes.descriptionContainer}>
        <Typography className={classes.descriptionTitle}>{t('descriptionTitle')}</Typography>
        <div className={classes.descriptionContent}>
          <Typography variant="body2">
            {t('description', {
              targetDbVersion: formatYbSoftwareVersionString(
                latestSoftwareUpgradeTask?.details?.versionNumbers?.ybSoftwareVersion ?? '-'
              )
            })}
          </Typography>
          {isYsqlMajorUpgrade && <Typography variant="body1">{t('ysqlMajorUpgrade')}</Typography>}
        </div>
      </div>
      {latestSoftwareUpgradeTask && universe && (
        <DbUpgradeProgressPanel
          dbUpgradeTask={latestSoftwareUpgradeTask}
          universe={universe}
          className={classes.progressPanel}
          isDbUpgradeFinalizeRequired={isDbUpgradeFinalizationRequired}
          isYsqlMajorUpgrade={isYsqlMajorUpgrade}
          onCloseSidePanel={modalProps.onClose}
        />
      )}
    </YBModal>
  );
};

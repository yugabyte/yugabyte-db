import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { YBModal, YBModalProps } from '@app/redesign/components';
import { TaskType } from '@app/redesign/features/tasks/dtos';
import {
  api,
  dbUpgradeMetadataQueryKey,
  taskQueryKey,
  universeQueryKey
} from '@app/redesign/helpers/api';
import { SortDirection } from '@app/redesign/utils/dtos';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { getUniverse, precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { DbUpgradeProgressPanel } from './DbUpgradeProgressPanel';
import { PollingIntervalMs } from '@app/components/xcluster/constants';

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
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.dbUpgradeManagementSidePanel'
  });

  const isPanelOpen = !!modalProps.open;

  const getPagedSoftwareUpgradeTasksRequest = {
    direction: SortDirection.DESC,
    filter: {
      typeList: [TaskType.SOFTWARE_UPGRADE],
      targetUUIDList: [universeUuid]
    }
  };
  const softwareUpgradeTasksQuery = useQuery(
    taskQueryKey.paged(getPagedSoftwareUpgradeTasksRequest),
    () => api.fetchPagedCustomerTasks(getPagedSoftwareUpgradeTasksRequest),
    { enabled: isPanelOpen && !!universeUuid, refetchInterval: PollingIntervalMs.FOCUSED_TASK }
  );

  const latestSoftwareUpgradeTask = softwareUpgradeTasksQuery.data?.entities[0];

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
  const isYsqlMajorUpgrade = dbUpgradeMetadataQuery.data?.ysql_major_version_upgrade ?? false;
  const universeDetailsQuery = useQuery(
    universeQueryKey.detailsV2(universeUuid),
    () => getUniverse(universeUuid),
    { enabled: isPanelOpen && !!universeUuid }
  );

  const universe = universeDetailsQuery.data;

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
      {latestSoftwareUpgradeTask && (
        <DbUpgradeProgressPanel
          dbUpgradeTask={latestSoftwareUpgradeTask}
          universe={universe}
          className={classes.progressPanel}
          isYsqlMajorUpgrade={isYsqlMajorUpgrade}
        />
      )}
    </YBModal>
  );
};

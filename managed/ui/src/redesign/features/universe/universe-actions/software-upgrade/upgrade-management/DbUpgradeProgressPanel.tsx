import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';

import { Task } from '@app/redesign/features/tasks/dtos';
import { getPrimaryCluster } from '@app/redesign/utils/universeUtils';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getPlacementAzMetadataList } from '../utils/formUtils';
import { AccordionCard, AccordionCardState } from './AccordionCard';

interface DbUpgradeProgressPanelProps {
  dbUpgradeTask: Task;
  universe: Universe | undefined;
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  progressPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(2),

    backgroundColor: theme.palette.grey[50],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    '& .MuiTypography-body2': {
      lineHeight: '20px'
    },
    '& .MuiTypography-subtitle1': {
      lineHeight: '18px'
    }
  },
  title: {
    color: theme.palette.grey[800],
    fontSize: 15,
    fontWeight: 600,
    fontStyle: 'normal',
    lineHeight: '20px'
  },
  upgradeStageCard: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2)
  }
}));

const TSERVER_AZ_UPGRADE_STAGE_START_INDEX = 3;

export const DbUpgradeProgressPanel = ({
  dbUpgradeTask,
  universe,
  className
}: DbUpgradeProgressPanelProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel'
  });

  const upgradedAzMetadataList =
    getPlacementAzMetadataList(getPrimaryCluster(universe?.spec?.clusters ?? [])) ?? [];
  const upgradedAzDisplayNameByUuid = Object.fromEntries(
    upgradedAzMetadataList.map((az) => [az.azUuid, az.displayName])
  );
  const upgradeAzStageCount =
    dbUpgradeTask.canaryUpgradeProgress?.tserverAZUpgradeStatesList.length ?? 0;
  return (
    <div className={clsx(classes.progressPanel, className)}>
      <Typography variant="h5" className={classes.title}>
        {t('title')}
      </Typography>
      <AccordionCard
        title={t('preCheckStage.title')}
        stepNumber={1}
        state={AccordionCardState.NEUTRAL}
      />
      <AccordionCard
        title={t('upgradeMasterServersStage.title')}
        stepNumber={2}
        state={AccordionCardState.NEUTRAL}
      />
      {dbUpgradeTask.canaryUpgradeProgress?.tserverAZUpgradeStatesList.map(
        (azUpgradeState, index) => (
          <AccordionCard
            key={azUpgradeState.azUUID}
            title={t('upgradeAzStage.title', {
              azLabel: upgradedAzDisplayNameByUuid[azUpgradeState.azUUID] ?? azUpgradeState.azName
            })}
            stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + index}
            state={AccordionCardState.NEUTRAL}
          />
        )
      )}
      <AccordionCard
        title={t('finalizeStage.title')}
        stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + upgradeAzStageCount}
        state={AccordionCardState.NEUTRAL}
      />
    </div>
  );
};

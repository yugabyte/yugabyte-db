import { makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';

import { usePillStyles } from '../../../../redesign/styles/styles';
import { getTableName } from '../../../../utils/tableUtils';
import { IndexTableReplicationCandidate, MainTableReplicationCandidate } from '../../XClusterTypes';
import { TableEligibilityPill } from './TableEligibilityPill';
import { shouldShowTableEligibilityPill } from './utils';

interface TableNameCellProps {
  tableReplicationCandidate: MainTableReplicationCandidate | IndexTableReplicationCandidate;
}

const useStyles = makeStyles((theme) => ({
  tableNameCell: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  tableName: {
    alignContent: 'center'
  }
}));
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.selectTable';

export const TableNameCell = ({ tableReplicationCandidate }: TableNameCellProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const pillClasses = usePillStyles();
  return (
    <div className={classes.tableNameCell}>
      <div className={classes.tableName}>{getTableName(tableReplicationCandidate)}</div>
      {shouldShowTableEligibilityPill(tableReplicationCandidate) && (
        <TableEligibilityPill eligibilityDetails={tableReplicationCandidate.eligibilityDetails} />
      )}
      {tableReplicationCandidate.isUnreplicatedTableInReplicatedNamespace && (
        <div className={clsx(pillClasses.pill, pillClasses.warning)}>
          {t('tablePill.unreplicatedTableInReplicatedNamespace')}
        </div>
      )}
    </div>
  );
};

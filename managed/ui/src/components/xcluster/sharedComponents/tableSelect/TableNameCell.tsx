import { makeStyles } from '@material-ui/core';
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

export const TableNameCell = ({ tableReplicationCandidate }: TableNameCellProps) => {
  const classes = useStyles();
  return (
    <div className={classes.tableNameCell}>
      <div className={classes.tableName}>{getTableName(tableReplicationCandidate)}</div>
      {shouldShowTableEligibilityPill(tableReplicationCandidate) && (
        <TableEligibilityPill eligibilityDetails={tableReplicationCandidate.eligibilityDetails} />
      )}
    </div>
  );
};

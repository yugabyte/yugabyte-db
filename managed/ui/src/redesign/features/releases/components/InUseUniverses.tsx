import { Link } from 'react-router';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { ReleaseUniverses } from './dtos';
import { convertToLocalTime } from '../../../../components/xcluster/ReplicationUtils';
import { useSelector } from 'react-redux';

interface InUseUniversesProps {
  inUseUniverses: ReleaseUniverses[] | undefined;
}

const useStyles = makeStyles((theme) => ({
  header: {
    fontWeight: 600,
    fontFamily: 'Inter',
    fontSize: '13px'
  },
  noUniversesMessage: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '13px'
  }
}));

export const InUseUniverses = ({ inUseUniverses }: InUseUniversesProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const inUseUniverseslength = inUseUniverses?.length ?? 0;

  const formatName = (cell: any, row: any) => {
    return (
      <Box>
        <Link to={`/universes/${row.uuid}`} target="_blank">
          <span style={{ textDecoration: 'underline' }}>{row.name}</span>
        </Link>
      </Box>
    );
  };

  const formatCreationDate = (cell: any, row: any) => {
    const creationDate = row.creation_date;
    const localCreationTime = convertToLocalTime(creationDate, currentUserTimezone);

    return (
      <Box>
        <span>{localCreationTime}</span>
      </Box>
    );
  };

  return (
    <Box>
      <span className={helperClasses.header}>{`Universes (${inUseUniverseslength})`}</span>
      {inUseUniverseslength > 0 ? (
        <BootstrapTable data={inUseUniverses!}>
          <TableHeaderColumn dataField={'uuid'} isKey={true} hidden={true} />
          <TableHeaderColumn width="50%" dataFormat={formatName} />
          <TableHeaderColumn width="50%" dataFormat={formatCreationDate}>
            {t('releases.creationDate')}
          </TableHeaderColumn>
        </BootstrapTable>
      ) : (
        <Box mt={2}>
          <span className={helperClasses.noUniversesMessage}>
            {t('releases.noUniversesWithRelease')}
          </span>
        </Box>
      )}
    </Box>
  );
};

// creation_date
// moment.utc(healthCheckData.timestamp).local();

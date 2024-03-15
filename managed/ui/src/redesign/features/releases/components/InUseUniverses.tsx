import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { ReleaseUniverses } from './dtos';
import { Link } from 'react-router';

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

  const inUseUniverseslength = inUseUniverses?.length ?? 0;

  const formatName = (cell: any, row: any) => {
    return (
      <Box>
        <Link to={`/universes/${row.uuid}`}>
          <span style={{ textDecoration: 'underline' }}>{row.name}</span>
        </Link>
      </Box>
    );
  };

  return (
    <Box>
      <span className={helperClasses.header}>{`Universes (${inUseUniverseslength})`}</span>
      {inUseUniverseslength > 0 ? (
        <BootstrapTable data={inUseUniverses!}>
          <TableHeaderColumn dataField={'id'} isKey={true} hidden={true} />
          <TableHeaderColumn width="50%" dataFormat={formatName} />
          <TableHeaderColumn width="50%" dataField={'creation_date'}>
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

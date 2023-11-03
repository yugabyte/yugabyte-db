import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBModal } from '../../../../../../components';
import { Gflag } from '../../../utils/dto';

interface ReadOnlyGflagModalProps {
  gFlags: Gflag[];
  open: boolean;
  onClose: () => void;
}

export const ReadOnlyGflagsModal = ({
  gFlags,
  open,
  onClose
}: ReadOnlyGflagModalProps): ReactElement => {
  const { t } = useTranslation();
  return (
    <YBModal
      title={t('universeForm.gFlags.primaryClusterFlags')}
      open={open}
      onClose={onClose}
      size="md"
      overrideHeight={'420px'}
      titleSeparator
    >
      <Box
        display={'flex'}
        height={'100%'}
        className="gflag-read-table"
        width="100%"
        flexDirection="column"
        overflow={'auto'}
      >
        <BootstrapTable
          data={gFlags}
          height={'auto'}
          maxHeight={'420px'}
          tableStyle={{ overflow: 'scroll' }}
        >
          <TableHeaderColumn
            width={'40%'}
            dataField="Name"
            dataFormat={(cell) => <span className="cell-font">{cell}</span>}
            isKey
          >
            <span className="header-title">{t('universeForm.gFlags.flagName')}</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="TSERVER"
            width={'30%'}
            dataFormat={(cell) => (
              <span className="cell-font">{cell !== undefined ? `${cell}` : ''}</span>
            )}
          >
            <span className="header-title">{t('universeForm.gFlags.tServerValue')}</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="MASTER"
            width="30%"
            dataFormat={(cell) => (
              <span className="cell-font">{cell !== undefined ? `${cell}` : ''}</span>
            )}
          >
            <span className="header-title">{t('universeForm.gFlags.masterValue')}</span>
          </TableHeaderColumn>
        </BootstrapTable>
      </Box>
    </YBModal>
  );
};

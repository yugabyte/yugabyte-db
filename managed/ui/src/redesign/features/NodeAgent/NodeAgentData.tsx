import { FC, useState } from 'react';
import { DropdownButton, MenuItem, Tooltip } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { Box, makeStyles } from '@material-ui/core';
import { DeleteNodeAgent } from './DeleteNodeAgent';
import { NodeAgentStatus } from './NodeAgentStatus';
import { YBPanelItem } from '../../../components/panels';
import { calculateDuration } from '../../../components/backupv2/common/BackupUtils';
import { OnPremProvider } from '../../../components/configRedesign/providerRedesign/types';
import { NodeAgentEntities } from '../../utils/dtos';
import VersionMisatch from '../../assets/version-mismatch.svg';

const useStyles = makeStyles((theme) => ({
  selectBox: {
    minWidth: '150px'
  },
  columnName: {
    fontWeight: 700,
    fontSize: theme.spacing(1.5),
    fontFamily: 'Inter'
  },
  versionMismatchImage: {
    marginLeft: theme.spacing(0.5),
    marginBottom: theme.spacing(0.5)
  },
  tagTextBlue: {
    color: '#1A44A5'
  },
  tagTextGreen: {
    color: '#097245'
  },
  tagTextRed: {
    color: '#8F0000'
  },
  tagGreen: {
    backgroundColor: '#CDEFE1'
  },
  tagRed: {
    backgroundColor: '#FDE2E2'
  },
  tagBlue: {
    backgroundColor: '#CBDAFF'
  },
  nodeAgentStateTag: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    height: '24px',
    borderRadius: '6px',
    padding: '10px 6px',
    width: 'fit-content'
  },
  nodeAgentStatusText: {
    fontFamily: 'Inter',
    fontStyle: 'normal',
    fontWeight: 400,
    fontSize: '11.5px',
    lineHeight: '16px'
  },
  nodeAgentStatusTable: {
    padding: theme.spacing(2),
    border: '1px solid #E5E5E9',
    borderRadius: theme.spacing(0.5),
    background: '#FFFFFF'
  },
  actionsDropdown: {
    marginTop: '-12px'
  }
}));

interface NodeAgentDataProps {
  isAssignedNodes: boolean;
  nodeAgentData: NodeAgentEntities[];
  isNodeAgentDebugPage?: boolean;
  onNodeAgentDeleted?: () => void;
}

export const NodeAgentData: FC<NodeAgentDataProps> = ({
  isAssignedNodes,
  nodeAgentData,
  isNodeAgentDebugPage = true,
  onNodeAgentDeleted
}) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();
  const providers = useSelector((state: any) => state.cloud.providers.data);
  const [openNodeAgentDialog, setOpenNodeAgentDialog] = useState<boolean>(false);
  const [nodeAgentUuid, setNodeAgentUuid] = useState<string>('');
  const [providerName, setProviderName] = useState<string>('');
  const ybaVersionResponse = useSelector((state: any) => state.customer.yugawareVersion);
  const ybaVersion = ybaVersionResponse.data.version;

  const formatState = (cell: any, row: any) => {
    return <NodeAgentStatus status={row?.state} isReachable={row?.reachable} />;
  };

  const formatVersion = (cell: any, row: any) => {
    const tooltipMessage = `The node agent version ${row.version} is not synchronized with the
     YugabyteDB Anywhere version ${ybaVersion}.
     The node agent will be automatically upgraded 
     by YugabyteDB Anywhere in the near future.`;

    return (
      <Box>
        <span>{row.version}</span>
        {!row.versionMatched && (
          <Tooltip title={tooltipMessage} placement="top">
            <img
              className={helperClasses.versionMismatchImage}
              src={VersionMisatch}
              alt="mismatch"
            />
          </Tooltip>
        )}
      </Box>
    );
  };

  const formatUpdatedAt = (cell: any, row: any) => {
    const rowDate = row.updatedAt;
    const currentUTCTime = new Date().toISOString();
    const nodeAgentUpdatedUTCTime = new Date(rowDate).toISOString();
    const diffTime = calculateDuration(nodeAgentUpdatedUTCTime, currentUTCTime, true);
    return <span>{diffTime}</span>;
  };

  const formatActionButtons = (cell: any, row: any) => {
    const providerUuid = row.providerUuid;
    const matchingProvider = providers?.find(
      (provider: OnPremProvider) => provider.uuid === providerUuid
    );
    const skipProvisioning = !!matchingProvider?.details.skipProvisioning;

    return (
      <DropdownButton
        className={helperClasses.actionsDropdown}
        title="Actions"
        disabled={!skipProvisioning}
        id="runtime-config-nested-dropdown middle-aligned-table"
        pullRight
      >
        <MenuItem
          onClick={() => {
            openDeleteDialog(row);
          }}
        >
          {t('nodeAgent.delete')}
        </MenuItem>
      </DropdownButton>
    );
  };

  const openDeleteDialog = (row: any) => {
    setProviderName(row.providerName);
    setNodeAgentUuid(row.uuid);
    setOpenNodeAgentDialog(true);
  };

  return (
    <Box>
      <YBPanelItem
        body={
          <BootstrapTable
            data={nodeAgentData}
            pagination={nodeAgentData.length > 10}
            containerClass={helperClasses.nodeAgentStatusTable}
          >
            <TableHeaderColumn dataField="nodeAgentID" isKey={true} hidden={true} />
            <TableHeaderColumn
              width="15%"
              className={'middle-aligned-table'}
              columnClassName={'yb-table-cell yb-table-cell-align'}
              dataField={'name'}
              dataSort
            >
              <span className={helperClasses.columnName}>{'Node Name'}</span>
            </TableHeaderColumn>

            {isAssignedNodes && isNodeAgentDebugPage && (
              <TableHeaderColumn
                width="15%"
                className={'middle-aligned-table'}
                columnClassName={'yb-table-cell yb-table-cell-align'}
                dataField={'universeName'}
                dataSort
              >
                <span className={helperClasses.columnName}>{'Universe Name'}</span>
              </TableHeaderColumn>
            )}

            {!isAssignedNodes && isNodeAgentDebugPage && (
              <TableHeaderColumn
                width="15%"
                className={'middle-aligned-table'}
                columnClassName={'yb-table-cell yb-table-cell-align'}
                dataField={'providerName'}
                dataSort
              >
                <span className={helperClasses.columnName}>{'Provider Name'}</span>
              </TableHeaderColumn>
            )}

            <TableHeaderColumn
              dataField={'ip'}
              width="10%"
              columnClassName={'yb-table-cell yb-table-cell-align'}
              dataSort
            >
              <span className={helperClasses.columnName}>{'Agent Address'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width="15%"
              dataFormat={formatUpdatedAt}
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Time since heartbeat'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              dataFormat={formatState}
              width="10%"
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Agent Status'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width="15%"
              dataFormat={formatVersion}
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Version'}</span>
            </TableHeaderColumn>
            {!isAssignedNodes && (
              <TableHeaderColumn
                dataField={'actions'}
                columnClassName={'yb-actions-cell'}
                width="10%"
                dataFormat={formatActionButtons}
              >
                <span className={helperClasses.columnName}>{'Actions'}</span>
              </TableHeaderColumn>
            )}
          </BootstrapTable>
        }
        noBackground
      />
      {openNodeAgentDialog && (
        <DeleteNodeAgent
          openNodeAgentDialog={openNodeAgentDialog}
          providerName={providerName}
          nodeAgentUUID={nodeAgentUuid}
          onNodeAgentDeleted={onNodeAgentDeleted}
          onClose={() => setOpenNodeAgentDialog(false)}
        />
      )}
    </Box>
  );
};

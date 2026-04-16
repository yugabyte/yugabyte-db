import { FC, useState } from 'react';
import { DropdownButton, MenuItem, Tooltip } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { useQuery } from 'react-query';

import { DeleteNodeAgent } from './DeleteNodeAgent';
import { NodeAgentStatus } from './NodeAgentStatus';
import { YBPanelItem } from '../../../components/panels';
import { calculateDuration } from '../../../components/backupv2/common/BackupUtils';
import { ProviderCode } from '../../../components/configRedesign/providerRedesign/constants';
import { InstallNodeAgentModal } from '../universe/universe-actions/install-node-agent/InstallNodeAgentModal';

import { YBProvider } from '../../../components/configRedesign/providerRedesign/types';
import { NodeAgent } from '../../utils/dtos';
import VersionMisatch from '../../assets/version-mismatch.svg?img';
import { api, universeQueryKey } from '../../helpers/api';
import { AugmentedNodeAgent } from './types';
import { SortOrder } from '../../helpers/constants';
import { YBTooltip } from '../../components';

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
  nodeAgents: NodeAgent[];
  isErrorFilterChecked: boolean;

  isNodeAgentDebugPage?: boolean;
  onNodeAgentDeleted?: () => void;
}

const TRANSLATION_KEY_PREFIX = 'nodeAgent';

export const NodeAgentData: FC<NodeAgentDataProps> = ({
  isAssignedNodes,
  nodeAgents,
  isErrorFilterChecked,
  isNodeAgentDebugPage = true,
  onNodeAgentDeleted
}) => {
  const helperClasses = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const providers = useSelector((state: any) => state.cloud.providers.data);
  const [isDeleteNodeAgentModalOpen, setIsDeleteNodeAgentModalOpen] = useState<boolean>(false);
  const [isInstallNodeAgentModalOpen, setIsInstallNodeAgentModalOpen] = useState<boolean>(false);
  const [selectedNodeAgent, setSelectedNodeAgent] = useState<any>(undefined);
  const ybaVersionResponse = useSelector((state: any) => state.customer.yugawareVersion);
  const ybaVersion = ybaVersionResponse.data.version;

  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  const formatState = (_: any, nodeAgent: AugmentedNodeAgent) => (
    <NodeAgentStatus nodeAgent={nodeAgent} />
  );

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

  const formatUpdatedAt = (_: any, row: any) => {
    const rowDate = row.updatedAt;
    const currentUTCTime = new Date().toISOString();
    const nodeAgentUpdatedUTCTime = new Date(rowDate).toISOString();
    const diffTime = calculateDuration(nodeAgentUpdatedUTCTime, currentUTCTime, true);
    return <span>{diffTime}</span>;
  };

  const formatNodeAgentActions = (_: any, row: any) => {
    const providerUuid = row.providerUuid;
    const provider = providers?.find(
      (provider: YBProvider) => provider.uuid === providerUuid
    ) as YBProvider;
    const skipProvisioning = !!provider?.details.skipProvisioning;

    const isInstallNodeAgentDisabled = skipProvisioning && provider?.code === ProviderCode.ON_PREM;
    return (
      <DropdownButton
        className={helperClasses.actionsDropdown}
        title="Actions"
        id="runtime-config-nested-dropdown middle-aligned-table"
        pullRight
      >
        {row.universeUuid && (
          <MenuItem
            onSelect={() => {
              openInstallNodeAgentModal(row);
            }}
            disabled={isInstallNodeAgentDisabled}
          >
            <YBTooltip
              title={
                isInstallNodeAgentDisabled
                  ? t('action.install.onPremManuallyProvisionedTooltip')
                  : ''
              }
              placement="top"
            >
              <Typography variant="body2">{t('action.install.label')}</Typography>
            </YBTooltip>
          </MenuItem>
        )}
        <MenuItem
          onSelect={() => {
            openDeleteDialog(row);
          }}
        >
          <Typography variant="body2">{t('action.delete.label')}</Typography>
        </MenuItem>
      </DropdownButton>
    );
  };

  const openInstallNodeAgentModal = (row: any) => {
    setIsInstallNodeAgentModalOpen(true);
    setSelectedNodeAgent(row);
  };
  const closeInstallNodeAgentModal = () => {
    setIsInstallNodeAgentModalOpen(false);
  };

  const openDeleteDialog = (row: any) => {
    setSelectedNodeAgent(row);
    setIsDeleteNodeAgentModalOpen(true);
  };

  const getNodeAgentStatusLabel = (nodeAgent: NodeAgent) =>
    nodeAgent.reachable
      ? t(`statusLabel.${nodeAgent.state}`, 'statusLabel.unknownStatus')
      : t('statusLabel.unreachable');

  const getNodeAgentErrorLabel = (nodeAgent: NodeAgent) =>
    universeListQuery.data?.find((universe) => universe.universeUUID === nodeAgent.universeUuid)
      ?.universeDetails.universePaused && nodeAgent.lastError
      ? t('errorLabel.universePaused')
      : nodeAgent.lastError
      ? t(`errorLabel.${nodeAgent.lastError.code}`)
      : '';

  /**
   * Node agent objects with additional fields added on the client side.
   */
  const augmentedNodeAgents = nodeAgents.map(
    (nodeAgent): AugmentedNodeAgent => ({
      ...nodeAgent,
      statusLabel: getNodeAgentStatusLabel(nodeAgent),
      errorLabel: getNodeAgentErrorLabel(nodeAgent)
    })
  );
  const filteredNodeAgents = isErrorFilterChecked
    ? augmentedNodeAgents.filter((nodeAgent) => nodeAgent.lastError?.code)
    : augmentedNodeAgents;

  return (
    <Box>
      <YBPanelItem
        body={
          <BootstrapTable
            data={filteredNodeAgents}
            pagination={filteredNodeAgents.length > 10}
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
              <span className={helperClasses.columnName}>{'Node Address'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="updatedAt"
              sortFunc={(a: AugmentedNodeAgent, b: AugmentedNodeAgent, order: string) =>
                // Sort in reverse order since the data is processed and displayed as duration (time since heartbeat).
                order === SortOrder.ASCENDING
                  ? b.updatedAt.localeCompare(a.updatedAt)
                  : a.updatedAt.localeCompare(b.updatedAt)
              }
              dataSort
              width="15%"
              dataFormat={formatUpdatedAt}
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Time since heartbeat'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="statusLabel"
              dataSort
              dataFormat={formatState}
              width="10%"
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Agent Status'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="errorLabel"
              dataSort
              width="15%"
              columnClassName={'yb-table-cell yb-table-cell-align'}
            >
              <span className={helperClasses.columnName}>{'Error'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width="15%"
              dataFormat={formatVersion}
              columnClassName={'yb-table-cell yb-table-cell-align'}
              dataField="version"
              dataSort
            >
              <span className={helperClasses.columnName}>{'Version'}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField={'actions'}
              columnClassName={'yb-actions-cell'}
              width="10%"
              dataFormat={formatNodeAgentActions}
            >
              <span className={helperClasses.columnName}>{'Actions'}</span>
            </TableHeaderColumn>
          </BootstrapTable>
        }
        noBackground
      />
      {isDeleteNodeAgentModalOpen && (
        <DeleteNodeAgent
          openNodeAgentDialog={isDeleteNodeAgentModalOpen}
          nodeName={selectedNodeAgent.name}
          nodeAgentUUID={selectedNodeAgent.uuid}
          onNodeAgentDeleted={onNodeAgentDeleted}
          onClose={() => setIsDeleteNodeAgentModalOpen(false)}
        />
      )}
      {isInstallNodeAgentModalOpen && (
        <InstallNodeAgentModal
          universeUuid={selectedNodeAgent.universeUuid}
          isUniverseAction={false}
          isReinstall={true}
          nodeName={selectedNodeAgent.name}
          modalProps={{ open: isInstallNodeAgentModalOpen, onClose: closeInstallNodeAgentModal }}
        />
      )}
    </Box>
  );
};

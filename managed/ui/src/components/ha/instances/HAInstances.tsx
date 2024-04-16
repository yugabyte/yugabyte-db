import _ from 'lodash';
import { FC, ReactElement, useState } from 'react';
import { Col, Grid, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import moment from 'moment-timezone';

import { YBButton } from '../../common/forms/fields';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { YBLoading } from '../../common/indicators';
import { HAErrorPlaceholder } from '../compounds/HAErrorPlaceholder';
import { DeleteModal } from '../modals/DeleteModal';
import { PromoteInstanceModal } from '../modals/PromoteInstanceModal';
import { BadgeInstanceType } from '../compounds/BadgeInstanceType';
import { AddStandbyInstanceModal } from '../modals/AddStandbyInstanceModal';
import { formatDuration } from '../../../utils/Formatters';

import { HaInstanceState, HaPlatformInstance } from '../dtos';

import './HAInstances.scss';
import { HAInstanceStatelabel } from '../compounds/HAInstanceStateLabel';

interface HAInstancesProps {
  // Dispatch
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string) => void;
  // State
  runtimeConfigs: any;
}

const renderAddress = (cell: any, row: HaPlatformInstance): ReactElement => (
  <a href={row.address} target="_blank" rel="noopener noreferrer">
    {row.address}
    {row.is_local && <span className="badge badge-orange">Current</span>}
  </a>
);

const renderInstanceType = (cell: HaPlatformInstance['is_leader']): ReactElement => (
  <BadgeInstanceType isActive={cell} />
);

const renderBackupLag = (cell: HaPlatformInstance['last_backup']): ReactElement | string =>
  cell ? formatDuration(moment.duration(moment().diff(moment(cell))).asMilliseconds()) : 'n/a';

export const HAInstances: FC<HAInstancesProps> = ({
  fetchRuntimeConfigs,
  setRuntimeConfig,
  runtimeConfigs
}) => {
  const [isAddInstancesModalVisible, setAddInstancesModalVisible] = useState(false);
  const [instanceToDelete, setInstanceToDelete] = useState<string>();
  const [instanceToPromote, setInstanceToPromote] = useState<string>();
  const { config, error, isNoHAConfigExists, isLoading } = useLoadHAConfiguration({
    loadSchedule: false,
    autoRefresh: true
  });

  const showAddInstancesModal = () => setAddInstancesModalVisible(true);
  const hideAddInstancesModal = () => setAddInstancesModalVisible(false);
  const showDeleteModal = (instanceId: string) => setInstanceToDelete(instanceId);
  const hideDeleteModal = () => setInstanceToDelete(undefined);
  const showPromoteModal = (instanceId: string) => setInstanceToPromote(instanceId);
  const hidePromoteModal = () => setInstanceToPromote(undefined);

  const currentInstance = config?.instances.find((item) => item.is_local);

  const renderActions = (cell: any, row: HaPlatformInstance): ReactElement => {
    if (currentInstance?.is_leader) {
      return (
        <YBButton
          btnText="Delete Instance"
          btnIcon="fa fa-trash"
          disabled={row.is_leader}
          onClick={() => showDeleteModal(row.uuid)}
        />
      );
    } else {
      // eslint-disable-next-line no-lonely-if
      if (row.is_local) {
        return (
          <YBButton
            btnText="Make Active"
            btnClass="btn btn-orange"
            btnIcon="fa fa-upload"
            onClick={() => showPromoteModal(row.uuid)}
          />
        );
      } else {
        // function needs to return at least smth
        return <span />;
      }
    }
  };

  if (isLoading) {
    return <YBLoading />;
  }

  if (error) {
    // eslint-disable-next-line @typescript-eslint/no-non-null-asserted-optional-chain
    return <HAErrorPlaceholder error={error} configUUID={config?.uuid!} />;
  }

  if (isNoHAConfigExists) {
    return (
      <div className="ha-instances__no-config" data-testid="ha-instances-no-config">
        <i className="fa fa-file-o" />
        <div>You must create a replication configuration first</div>
      </div>
    );
  }

  if (config && currentInstance) {
    const sortedInstances = _.sortBy(config.instances, [(item) => !item.is_leader, 'address']);
    return (
      <Grid fluid className="ha-instances">
        <AddStandbyInstanceModal
          configId={config.uuid}
          visible={isAddInstancesModalVisible}
          onClose={hideAddInstancesModal}
          fetchRuntimeConfigs={fetchRuntimeConfigs}
          setRuntimeConfig={setRuntimeConfig}
          runtimeConfigs={runtimeConfigs}
        />
        <DeleteModal
          configId={config.uuid}
          instanceId={instanceToDelete}
          visible={!!instanceToDelete}
          onClose={hideDeleteModal}
        />
        <PromoteInstanceModal
          configId={config.uuid}
          instanceId={instanceToPromote!}
          visible={!!instanceToPromote}
          onClose={hidePromoteModal}
        />

        <Row className="ha-instances__header">
          <Col xs={6}>
            <h4>Instances</h4>
            {!currentInstance.is_leader && (
              <span>Standby instances are configured from an active platform instance</span>
            )}
          </Col>
          <Col xs={6} className="ha-instances__header-buttons">
            {currentInstance.is_leader && (
              <YBButton
                btnText="Add Instance"
                btnClass="btn btn-orange"
                btnIcon="fa fa-plus"
                onClick={showAddInstancesModal}
              />
            )}
          </Col>
        </Row>
        <Row>
          <Col xs={12}>
            <BootstrapTable data={sortedInstances}>
              <TableHeaderColumn dataField="uuid" isKey hidden />
              <TableHeaderColumn
                dataField="address"
                dataFormat={renderAddress}
                dataSort
                width="30%"
              >
                Address
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="instance_state"
                dataFormat={(_: HaInstanceState, haInstance: HaPlatformInstance) => (
                  <HAInstanceStatelabel haInstance={haInstance} />
                )}
                dataSort
                width="17.5%"
              >
                Instance State
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="is_leader"
                dataFormat={renderInstanceType}
                dataSort
                width="17.5%"
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="last_backup"
                dataFormat={renderBackupLag}
                dataSort
                width="17.5%"
              >
                Time since last backup
              </TableHeaderColumn>
              <TableHeaderColumn
                columnClassName="yb-actions-cell"
                dataFormat={renderActions}
                width="17.5%"
              >
                Action
              </TableHeaderColumn>
            </BootstrapTable>
          </Col>
        </Row>
      </Grid>
    );
  }

  return <div>Oops</div>; // should never get here
};

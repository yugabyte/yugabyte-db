// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FieldArray, SubmissionError } from 'redux-form';
import { Link, withRouter } from 'react-router';
import { DropdownButton, MenuItem } from 'react-bootstrap';

import { YBConfirmModal } from '../../modals';
import { YBCodeBlock, YBCopyButton } from '../../common/descriptors/index';
import InstanceTypeForRegion from '../OnPrem/wizard/InstanceTypeForRegion';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { isNonEmptyObject, isNonEmptyArray } from '../../../utils/ObjectUtils';
import { YBButton, YBModal } from '../../common/forms/fields';
import { YBBreadcrumb } from '../../common/descriptors';
import { isDefinedNotNull, isNonEmptyString } from '../../../utils/ObjectUtils';
import { getLatestAccessKey } from '../../configRedesign/providerRedesign/utils';
import { TASK_SHORT_TIMEOUT } from '../../tasks/constants';
import { NodeAgentStatus } from '../../../redesign/features/NodeAgent/NodeAgentStatus';
import { OnPremNodeState } from '../../../redesign/helpers/dtos';

const TIMEOUT_BEFORE_REFRESH = 2500;

const PRECHECK_STATUS_ORDER = [
  'None',
  'Initializing',
  'Created',
  'Running',
  'Success',
  'Failure',
  'Aborted'
];

class OnPremNodesList extends Component {
  constructor(props) {
    super(props);
    this.state = {
      nodeToBeDeleted: {},
      nodeToBePrechecked: {},
      tasksPolling: false,
      nodeToBeRecommissioned: {}
    };
  }

  addNodeToList = () => {
    this.props.showAddNodesDialog();
  };

  hideAddNodeModal = () => {
    this.props.reset();
    this.props.hideDialog();
  };

  showConfirmDeleteModal(row) {
    this.setState({ nodeToBeDeleted: row });
    this.props.showConfirmDeleteModal();
  }

  hideDeleteNodeModal = () => {
    this.setState({ nodeToBeDeleted: {} });
    this.props.hideDialog();
  };

  showConfirmPrecheckModal(row) {
    this.setState({ nodeToBePrechecked: row });
    this.props.showConfirmPrecheckModal();
  }

  hidePrecheckNodeModal = () => {
    this.setState({ nodeToBePrechecked: {} });
    this.props.hideDialog();
  };

  showConfirmRecommissionNodeModal(row) {
    this.setState({ nodeToBeRecommissioned: row });
    this.props.showConfirmRecommissionNodeModal();
  }

  hideRecommissionNodeModal = () => {
    this.setState({ nodeToBeRecommissioned: {} });
    this.props.hideDialog();
  };

  precheckInstance = () => {
    const row = this.state.nodeToBePrechecked;
    if (!row.inUse) {
      const onPremProvider = this.findProvider();
      this.props.precheckInstance(onPremProvider.uuid, row.ip);

      setTimeout(() => {
        this.props.fetchCustomerTasks();
      }, TIMEOUT_BEFORE_REFRESH);
    }
    this.hidePrecheckNodeModal();
  };

  deleteInstance = () => {
    const onPremProvider = this.findProvider();
    const row = this.state.nodeToBeDeleted;
    if (!row.inUse) {
      this.props.deleteInstance(onPremProvider.uuid, row.ip);
    }
    this.hideDeleteNodeModal();
  };

  recommissionInstance = () => {
    const onPremProvider = this.findProvider();
    const node = this.state.nodeToBeRecommissioned;
    if (!node.inUse) {
      this.props.recommissionInstance(onPremProvider.uuid, node.ip);
    }
    this.hideRecommissionNodeModal();
  };

  findProvider = () => {
    const {
      cloud: { providers },
      selectedProviderUUID
    } = this.props;
    return providers.data.find((provider) => provider.uuid === selectedProviderUUID);
  };

  submitAddNodesForm = (vals, dispatch, reduxProps) => {
    const {
      cloud: { supportedRegionList, nodeInstanceList, accessKeys },
      currentProvider
    } = this.props;
    const onPremProvider = currentProvider ?? this.findProvider();
    const self = this;
    const currentCloudRegions = currentProvider
      ? currentProvider.regions
      : supportedRegionList.data.filter((region) => region.provider.uuid === onPremProvider.uuid);
    const latestAccessKey = currentProvider
      ? getLatestAccessKey(currentProvider.allAccessKeys)
      : accessKeys.data
          .filter((accessKey) => accessKey.idKey.providerUUID === onPremProvider.uuid)
          .shift();
    // function to construct list of all zones in current configuration
    const zoneList = currentCloudRegions.reduce(function (azs, r) {
      azs[r.code] = [];
      r.zones.map((z) => (azs[r.code][z.code.trim()] = z.uuid));
      return azs;
    }, {});

    // function takes in node list and returns node object keyed by zone
    const getInstancesKeyedByZone = (instances, region, zoneList) => {
      if (isNonEmptyArray(instances[region])) {
        return instances[region].reduce((acc, val) => {
          if (isNonEmptyObject(val) && isNonEmptyString(val.zone)) {
            const currentZone = val.zone.trim();
            const instanceName = isNonEmptyString(val.instanceName) ? val.instanceName.trim() : '';
            const currentZoneUUID = zoneList[region][currentZone];
            acc[currentZoneUUID] = acc[currentZoneUUID] || [];
            acc[currentZoneUUID].push({
              zone: currentZone,
              region: region,
              ip: val.instanceTypeIP.trim(),
              instanceType: val.machineType,
              sshUser: isNonEmptyObject(latestAccessKey) ? latestAccessKey.keyInfo.sshUser : '',
              sshPort: isNonEmptyObject(latestAccessKey) ? latestAccessKey.keyInfo.sshPort : null,
              instanceName: instanceName
            });
          }
          return acc;
        }, {});
      } else {
        return null;
      }
    };

    // function to construct final payload to be sent to middleware
    let instanceTypeList = [];
    if (isNonEmptyObject(vals.instances)) {
      instanceTypeList = Object.keys(vals.instances)
        .map(function (region) {
          const instanceListByZone = getInstancesKeyedByZone(vals.instances, region, zoneList);
          return isNonEmptyObject(instanceListByZone) ? instanceListByZone : null;
        })
        .filter(Boolean);
      const existingNodeIps = new Set(
        nodeInstanceList.data.map((instance) => instance.details.ip.trim())
      );
      const errors = { instances: {} };
      Object.keys(vals.instances).forEach((region) => {
        vals.instances[region].forEach((az, index) => {
          // Check if IP address is already in use by other node instance
          if (az.instanceTypeIP) {
            if (existingNodeIps.has(az.instanceTypeIP.trim())) {
              // If array exists then there are multiple errors
              if (!Array.isArray(errors.instances[region])) {
                errors.instances[region] = [];
              }
              errors.instances[region][index] = {
                instanceTypeIP: `Duplicate IP error: ${az.instanceTypeIP}`
              };
            } else {
              // Add node instance to Set
              existingNodeIps.add(az.instanceTypeIP.trim());
            }
          }
        });
      });
      if (Object.keys(errors.instances).length) {
        // reduxProps.stopSubmit()
        throw new SubmissionError({
          ...errors,
          _error: 'Add node instance failed!'
        });
      } else if (isNonEmptyArray(instanceTypeList)) {
        self.props.createOnPremNodes(instanceTypeList, onPremProvider.uuid);
      } else {
        this.hideAddNodeModal();
      }
    }
    this.props.reset();
  };

  handleCheckNodesUsage = (cell, row) => {
    let result = 'n/a';
    const { universeList } = this.props;
    const isNodeInUse = row.state === OnPremNodeState.USED;

    if (isNodeInUse) {
      if (getPromiseState(universeList).isLoading() || getPromiseState(universeList).isInit()) {
        result = 'Loading...';
      } else if (getPromiseState(universeList).isSuccess()) {
        const universe = universeList.data.find((item) => {
          // TODO: match by nodeUuid when it's fully supported by universe
          return !!(item.universeDetails.nodeDetailsSet || []).find(
            (node) => node.nodeName && row.nodeName && node.nodeName === row.nodeName
          );
        });
        if (universe) {
          result = <Link to={`/universes/${universe.universeUUID}`}>{universe.name}</Link>;
        }
      }
    } else {
      result = 'NOT USED';
    }

    return result;
  };

  scheduleTasksPolling = () => {
    if (!this.state.tasksPolling) {
      this.timeout = setInterval(() => this.props.fetchCustomerTasks(), TASK_SHORT_TIMEOUT);
      this.setState({ tasksPolling: true });
    }
  };

  stopTasksPolling = () => {
    if (this.state.tasksPolling) {
      clearTimeout(this.timeout);
      this.setState({ tasksPolling: false });
    }
  };

  componentDidMount() {
    if (!getPromiseState(this.props.universeList).isSuccess()) {
      this.props.fetchUniverseList();
    }
  }

  componentDidUpdate(prevProps) {
    const { tasks } = this.props;
    if (tasks && isNonEmptyArray(tasks.customerTaskList)) {
      const activeTasks = tasks.customerTaskList.filter(
        (task) =>
          task.type === 'PrecheckNode' &&
          (task.status === 'Running' || task.status === 'Initializing')
      );
      if (activeTasks.length > 0) {
        this.scheduleTasksPolling();
      } else {
        this.stopTasksPolling();
      }
    }
  }

  render() {
    const {
      cloud: { nodeInstanceList, instanceTypes, supportedRegionList, accessKeys },
      handleSubmit,
      tasks: { customerTaskList },
      showProviderView,
      visibleModal,
      nodeAgentStatusByIPs
    } = this.props;
    const self = this;
    let nodeListItems = [];
    if (getPromiseState(nodeInstanceList).isSuccess()) {
      //finding prechecking tasks
      const lastTasks = new Map();
      if (isNonEmptyArray(customerTaskList)) {
        customerTaskList
          .filter((task) => task.type === 'PrecheckNode')
          .sort((a, b) => b.createTime.localeCompare(a.createTime))
          .forEach((task) => {
            if (!lastTasks.has(task.targetUUID)) {
              lastTasks.set(task.targetUUID, task);
            }
          });
      }
      const taskStatusOrder = (task) => {
        const status = task ? task.status : 'None';
        return PRECHECK_STATUS_ORDER.indexOf(status);
      };
      nodeListItems = nodeInstanceList.data.map(function (item) {
        return {
          nodeId: item.nodeUuid,
          nodeName: item.nodeName,
          inUse: item.inUse,
          ip: item.details.ip,
          instanceType: item.details.instanceType,
          region: item.details.region,
          zone: item.details.zone,
          zoneUuid: item.zoneUuid,
          instanceName: item.instanceName,
          state: item.state,
          precheckTask: lastTasks.get(item.nodeUuid),
          precheckStatus: taskStatusOrder(lastTasks.get(item.nodeUuid))
        };
      });
    }

    const renderIconByStatus = (task) => {
      if (!task) {
        return <div />;
      }
      const status = task.status;
      const errorIcon = (
        <i
          className="fa fa-warning yb-fail-color precheck-status-container"
          onClick={() => {
            window.open(`/tasks/${task.id}`);
          }}
        />
      );
      if (status === 'Created' || status === 'Initializing') {
        return <i className="fa fa-clock-o" />;
      } else if (status === 'Success') {
        return <i className="fa fa-check-circle yb-success-color" />;
      } else if (status === 'Running') {
        return <YBLoadingCircleIcon size="inline" />;
      } else if (status === 'Failure' || status === 'Aborted') {
        return errorIcon;
      }
      return errorIcon;
    };

    const isActive = (task) => {
      if (!task) {
        return false;
      }
      return !(task.status === 'Success' || task.status === 'Failure');
    };

    const precheckTaskItem = (cell, row) => {
      if (row) {
        return <div>{row.inUse ? '' : renderIconByStatus(row.precheckTask)}</div>;
      }
    };

    const nodeAgentStatus = (cell, row) => {
      let status = '';
      let isReachable = false;
      if (nodeAgentStatusByIPs.isSuccess) {
        const nodeAgents = nodeAgentStatusByIPs.data.entities;
        const nodeAgent = nodeAgents.find((nodeAgent) => row.ip === nodeAgent.ip);
        status = nodeAgent?.state;
        isReachable = nodeAgent?.reachable;
      }

      return <NodeAgentStatus status={status} isReachable={isReachable} />;
    };

    const actionsList = (cell, row) => {
      const precheckDisabled = row.inUse || isActive(row.precheckTask);
      const isNodeDecommissioned = row.state === OnPremNodeState.DECOMMISSIONED;
      const isNodeInUse = row.state === OnPremNodeState.USED;
      return (
        <>
          <DropdownButton
            className="ckup-config-actions btn btn-default"
            title="Actions"
            id="bg-nested-dropdown"
            pullRight
          >
            <MenuItem
              onClick={!precheckDisabled ? self.showConfirmPrecheckModal.bind(self, row) : null}
              disabled={precheckDisabled || isNodeDecommissioned}
            >
              <i className="fa fa-play-circle-o" />
              Perform check
            </MenuItem>
            <MenuItem
              onClick={
                !row.inUse || !isNodeInUse ? self.showConfirmDeleteModal.bind(self, row) : null
              }
              disabled={row.inUse || isNodeInUse}
            >
              <i className={`fa fa-trash`} />
              Delete node
            </MenuItem>
            {row.state === OnPremNodeState.DECOMMISSIONED && (
              <MenuItem onClick={self.showConfirmRecommissionNodeModal.bind(self, row)}>
                <i className={`fa fa-plus`} />
                Recommission Node
              </MenuItem>
            )}
          </DropdownButton>
        </>
      );
    };

    const onPremSetupReference =
      'https://docs.yugabyte.com/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/#configure-hardware-for-yugabytedb-nodes';
    let provisionMessage = <span />;
    const onPremProvider = this.props.currentProvider ?? this.findProvider();
    if (isDefinedNotNull(onPremProvider)) {
      const onPremKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === onPremProvider.uuid
      );
      if (
        onPremProvider.details.skipProvisioning ||
        (isDefinedNotNull(onPremKey) && onPremKey.keyInfo.skipProvisioning)
      ) {
        const provisionInstanceScript = `${
          onPremProvider.details.provisionInstanceScript ??
          onPremKey.keyInfo.provisionInstanceScript
        } --ask_password --ip <node IP Address> --mount_points <instance type mount points> ${
          onPremProvider.details.enableNodeAgent
            ? '--install_node_agent --api_token <API token> --yba_url <YBA URL> --node_name <name for the node> --instance_type <instance type name> --zone_name <name for the zone>'
            : ''
        }`;

        provisionMessage = (
          <Alert bsStyle="warning" className="pre-provision-message">
            <p>
              This provider is configured for manual provisioning. Before you can add instances, you
              must provision the nodes.
            </p>
            <p>
              If the SSH user has sudo access, run the provisioning script on each node using the
              following command:
            </p>
            <YBCodeBlock>
              {provisionInstanceScript}
              <YBCopyButton text={provisionInstanceScript} />
            </YBCodeBlock>
            {!!onPremProvider.details.enableNodeAgent && (
              <p>
                For YBA URL, provide the URL of your YBA machine (e.g.,
                http://ybahost.company.com:9000). The node must be able to reach YugabyteDB Anywhere
                at this address.
              </p>
            )}
            <p>
              If the SSH user does not have sudo access, you must set up each node manually. For
              information on the script options or setting up nodes manually, see the{' '}
              <a href={onPremSetupReference}>provider documentation.</a>
            </p>
          </Alert>
        );
      }
    }

    const currentCloudRegions = this.props.currentProvider
      ? this.props.currentProvider.regions
      : supportedRegionList.data.filter(
          (region) => region.provider.uuid === this.props.selectedProviderUUID
        );
    const regionFormTemplate = isNonEmptyArray(currentCloudRegions)
      ? currentCloudRegions
          .filter((regionItem) => regionItem.active)
          .map(function (regionItem, idx) {
            const zoneOptions = regionItem.zones
              .filter((zoneItem) => zoneItem.active)
              .map(function (zoneItem, zoneIdx) {
                return (
                  <option key={zoneItem + zoneIdx} value={zoneItem.code}>
                    {zoneItem.code}
                  </option>
                );
              });
            const machineTypeOptions = instanceTypes.data.map(function (machineTypeItem, mcIdx) {
              return (
                <option key={machineTypeItem + mcIdx} value={machineTypeItem.instanceTypeCode}>
                  {machineTypeItem.instanceTypeCode}
                </option>
              );
            });
            zoneOptions.unshift(
              <option key={-1} value={''}>
                Select
              </option>
            );
            machineTypeOptions.unshift(
              <option key={-1} value={''}>
                Select
              </option>
            );
            return (
              <div key={`instance${idx}`}>
                <div className="instance-region-type">{regionItem.code}</div>
                <div className="form-field-grid">
                  <FieldArray
                    name={`instances.${regionItem.code}`}
                    component={InstanceTypeForRegion}
                    zoneOptions={zoneOptions}
                    machineTypeOptions={machineTypeOptions}
                    formType={'modal'}
                  />
                </div>
              </div>
            );
          })
      : null;
    const deleteConfirmationText = `Are you sure you want to delete node${
      isNonEmptyObject(this.state.nodeToBeDeleted) && this.state.nodeToBeDeleted.nodeName
        ? ' ' + this.state.nodeToBeDeleted.nodeName
        : ''
    }?`;
    const recommisionNodeConfirmationText = `Are you sure you want to recommission node${
      isNonEmptyObject(this.state.nodeToBeRecommissioned) && this.state.nodeToBeRecommissioned.ip
        ? ' ' + this.state.nodeToBeRecommissioned.ip
        : ''
    }?`;
    const precheckConfirmationText = `Are you sure you want to run precheck on node${
      isNonEmptyObject(this.state.nodeToBePrechecked) ? ' ' + this.state.nodeToBePrechecked.ip : ''
    }?`;
    const modalAddressSpecificText = 'IP addresses/hostnames';

    return (
      <div className="onprem-node-instances">
        {!this.props.isRedesignedView && (
          <>
            <span className="buttons pull-right">
              <YBButton btnText="Add Instances" btnIcon="fa fa-plus" onClick={this.addNodeToList} />
            </span>
            <YBBreadcrumb to="/config/cloud/onprem" onClick={showProviderView}>
              On-Premises Datacenter Config
            </YBBreadcrumb>
            <h3 className="onprem-node-instances__title">Instances</h3>
          </>
        )}

        {provisionMessage}

        <Row>
          <Col xs={12}>
            <BootstrapTable
              data={nodeListItems}
              search
              multiColumnSearch
              options={{
                clearSearch: true
              }}
              containerClass="onprem-nodes-table"
            >
              <TableHeaderColumn dataField="nodeId" isKey={true} hidden={true} dataSort />
              <TableHeaderColumn dataField="instanceName" dataSort>
                Node Name
              </TableHeaderColumn>
              <TableHeaderColumn dataField="ip" dataSort>
                Address
              </TableHeaderColumn>
              <TableHeaderColumn dataField="precheckStatus" dataFormat={precheckTaskItem} dataSort>
                Preflight Check
              </TableHeaderColumn>
              <TableHeaderColumn dataField="inUse" dataFormat={this.handleCheckNodesUsage} dataSort>
                Universe Name
              </TableHeaderColumn>
              <TableHeaderColumn dataField="state" dataSort>
                State
              </TableHeaderColumn>
              <TableHeaderColumn dataField="region" dataSort>
                Region
              </TableHeaderColumn>
              <TableHeaderColumn dataField="zone" dataSort>
                Zone
              </TableHeaderColumn>
              <TableHeaderColumn dataField="instanceType" dataSort>
                Instance Type
              </TableHeaderColumn>
              {this.props.isNodeAgentHealthDown && (
                <TableHeaderColumn dataField="" dataFormat={nodeAgentStatus} dataSort>
                  Agent
                </TableHeaderColumn>
              )}
              <TableHeaderColumn
                dataField=""
                dataFormat={actionsList}
                columnClassName="yb-actions-cell"
                className="yb-actions-cell"
              >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          </Col>
        </Row>
        <YBModal
          title={'Add Instances'}
          formName={'AddNodeForm'}
          visible={visibleModal === 'AddNodesForm'}
          onHide={this.hideAddNodeModal}
          onFormSubmit={handleSubmit(this.submitAddNodesForm)}
          showCancelButton={true}
          submitLabel="Add"
          size="large"
        >
          <div className="on-prem-form-text">
            {`Enter ${modalAddressSpecificText} for the instances of each availability zone and instance type.`}
          </div>
          {regionFormTemplate}
        </YBModal>
        <YBConfirmModal
          name={'confirmDeleteNodeInstance'}
          title={'Delete Node'}
          hideConfirmModal={this.hideDeleteNodeModal}
          currentModal={'confirmDeleteNodeInstance'}
          visibleModal={visibleModal}
          onConfirm={this.deleteInstance}
          confirmLabel="Delete"
          cancelLabel="Cancel"
        >
          {deleteConfirmationText}
        </YBConfirmModal>
        <YBConfirmModal
          name={'confirmPrecheckNodeInstance'}
          title={'Perform preflight check'}
          hideConfirmModal={this.hidePrecheckNodeModal}
          currentModal={'confirmPrecheckNodeInstance'}
          visibleModal={visibleModal}
          onConfirm={this.precheckInstance}
          confirmLabel="Apply"
          cancelLabel="Cancel"
        >
          {precheckConfirmationText}
        </YBConfirmModal>
        <YBConfirmModal
          name={'confirmRecommissionNodeInstance'}
          title={'Recommission Node'}
          hideConfirmModal={this.hideRecommissionNodeModal}
          currentModal={'confirmRecommissionNodeInstance'}
          visibleModal={visibleModal}
          onConfirm={this.recommissionInstance}
          confirmLabel="Apply"
          cancelLabel="Cancel"
        >
          {recommisionNodeConfirmationText}
        </YBConfirmModal>
      </div>
    );
  }
}

export default withRouter(OnPremNodesList);

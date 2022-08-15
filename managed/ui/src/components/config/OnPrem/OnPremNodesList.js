// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FieldArray, SubmissionError } from 'redux-form';
import { Link, withRouter } from 'react-router';

import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { isNonEmptyObject, isNonEmptyArray } from '../../../utils/ObjectUtils';
import { YBButton, YBModal } from '../../common/forms/fields';
import InstanceTypeForRegion from '../OnPrem/wizard/InstanceTypeForRegion';
import { YBBreadcrumb } from '../../common/descriptors';
import { isDefinedNotNull, isNonEmptyString } from '../../../utils/ObjectUtils';
import { YBCodeBlock } from '../../common/descriptors/index';
import { YBConfirmModal } from '../../modals';
import { TASK_SHORT_TIMEOUT } from '../../tasks/constants';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YUGABYTE_TITLE } from '../../../config';

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
    this.state = { nodeToBeDeleted: {}, nodeToBePrechecked: {}, tasksPolling: false };
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

  findProvider = () => {
    const {
      cloud: { providers },
      selectedProviderUUID
    } = this.props;
    return providers.data.find((provider) => provider.uuid === selectedProviderUUID);
  };

  submitAddNodesForm = (vals, dispatch, reduxProps) => {
    const {
      cloud: { supportedRegionList, nodeInstanceList, accessKeys }
    } = this.props;
    const onPremProvider = this.findProvider();
    const self = this;
    const currentCloudRegions = supportedRegionList.data.filter(
      (region) => region.provider.uuid === onPremProvider.uuid
    );
    const currentCloudAccessKey = accessKeys.data
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
              sshUser: isNonEmptyObject(currentCloudAccessKey)
                ? currentCloudAccessKey.keyInfo.sshUser
                : '',
              sshPort: isNonEmptyObject(currentCloudAccessKey)
                ? currentCloudAccessKey.keyInfo.sshPort
                : null,
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

  handleCheckNodesUsage = (inUse, row) => {
    let result = 'n/a';
    const { universeList } = this.props;

    if (inUse) {
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
      visibleModal
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

    const actionsList = (cell, row) => {
      const precheckDisabled = row.inUse || isActive(row.precheckTask);
      return (
        <>
          <DropdownButton
            className="ckup-config-actions btn btn-default"
            title="Actions"
            id="bg-nested-dropdown"
            pullRight
          >
            <MenuItem
              onClick={self.showConfirmPrecheckModal.bind(self, row)}
              disabled={precheckDisabled}
            >
              <i className="fa fa-play-circle-o" />
              Perform check
            </MenuItem>
            <MenuItem onClick={self.showConfirmDeleteModal.bind(self, row)} disabled={row.inUse}>
              <i className={`fa fa-trash`} />
              Delete node
            </MenuItem>
          </DropdownButton>
        </>
      );
    };

    const onPremSetupReference = 'https://docs.yugabyte.com/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/';
    let provisionMessage = <span />;
    const onPremProvider = this.findProvider();
    if (isDefinedNotNull(onPremProvider)) {
      const onPremKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === onPremProvider.uuid
      );
      if (isDefinedNotNull(onPremKey) && onPremKey.keyInfo.skipProvisioning) {
        provisionMessage = (
          <Alert bsStyle="warning" className="pre-provision-message">
            You need to pre-provision your nodes, If the Provider SSH User has sudo privileges
            you can execute the following script on the {YUGABYTE_TITLE} <b> yugaware </b>
            container -or- the  YugabyteDB Anywhere host machine depending on your deployment
            type once for each instance that you add here.
            <YBCodeBlock>
              {onPremKey.keyInfo.provisionInstanceScript + ' --ip '}
              <b>{'<IP Address> '}</b>
              {'--mount_points '}
              <b>{'<instance type mount points>'}</b>
            </YBCodeBlock>
            See the On-premises Provider <a href={onPremSetupReference}> documentation </a> for 
            more details if the <b> Provider SSH User</b>  does not have <b>sudo</b> privileges.
          </Alert>
        );
      }
    }

    const currentCloudRegions = supportedRegionList.data.filter(
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
    const precheckConfirmationText = `Are you sure you want to run precheck on node${
      isNonEmptyObject(this.state.nodeToBePrechecked) ? ' ' + this.state.nodeToBePrechecked.ip : ''
    }?`;
    const modalAddressSpecificText = 'IP addresses/hostnames';
    return (
      <div className="onprem-node-instances">
        <span className="buttons pull-right">
          <YBButton btnText="Add Instances" btnIcon="fa fa-plus" onClick={this.addNodeToList} />
        </span>

        <YBBreadcrumb to="/config/cloud/onprem" onClick={showProviderView}>
          On-Premises Datacenter Config
        </YBBreadcrumb>
        <h3 className="onprem-node-instances__title">Instances</h3>

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
                Identifier
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
              <TableHeaderColumn dataField="region" dataSort>
                Region
              </TableHeaderColumn>
              <TableHeaderColumn dataField="zone" dataSort>
                Zone
              </TableHeaderColumn>
              <TableHeaderColumn dataField="instanceType" dataSort>
                Instance Type
              </TableHeaderColumn>
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
      </div>
    );
  }
}

export default withRouter(OnPremNodesList);

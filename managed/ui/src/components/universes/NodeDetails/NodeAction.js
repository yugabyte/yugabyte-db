// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import _ from 'lodash';
import PropTypes from 'prop-types';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { NodeActionModalContainer, NodeConnectModal } from '../../universes';
import { NodeOverridesModal } from '../UniverseForm/HelmOverrides';
import { YBLabelWithIcon } from '../../common/descriptors';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { downloadLogs } from '../../../actions/universe';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';

export default class NodeAction extends Component {
  constructor() {
    super();
    this.state = {
      showModal: false,
      actionType: null,
      overridesModal: false,
      currentNode: null
    };
    this.closeModal = this.closeModal.bind(this);
    this.closeOverridesModal = this.closeOverridesModal.bind(this);
    this.handleLiveQueryClick = this.handleLiveQueryClick.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    actionType: PropTypes.oneOf(['STOP', 'REMOVE'])
  };

  openModal = (actionType) => {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.row,
        actionType: actionType,
        showModal: true
      };
    });
  };

  closeModal() {
    this.setState({
      showModal: false
    });
  }

  openOverridesModal = (currentRow) => {
    this.setState({
      showModal: false,
      overridesModal: true,
      currentNode: currentRow.name
    });
  };

  closeOverridesModal() {
    this.setState({
      overridesModal: false,
      currentNode: null
    });
  }

  //Need to add caption to new actionTypes to paint in the UI
  static getCaption(actionType, dedicatedTo) {
    let caption = null;
    if (actionType === 'STOP') {
      caption = dedicatedTo ? 'Stop ' + NodeAction.processName(dedicatedTo) : 'Stop Processes';
    } else if (actionType === 'REMOVE') {
      caption = 'Remove Node';
    } else if (actionType === 'DELETE') {
      caption = 'Delete Node';
    } else if (actionType === 'RELEASE') {
      caption = 'Release Instance';
    } else if (actionType === 'REBOOT') {
      caption = 'Reboot Node';
    } else if (actionType === 'START') {
      caption = dedicatedTo ? 'Start ' + NodeAction.processName(dedicatedTo) : 'Start Processes';
    } else if (actionType === 'ADD') {
      caption = 'Add Node';
    } else if (actionType === 'CONNECT') {
      caption = 'Connect';
    } else if (actionType === 'START_MASTER') {
      caption = 'Start Master';
    } else if (actionType === 'LIVE_QUERIES') {
      caption = 'Show Live Queries';
    } else if (actionType === 'SLOW_QUERIES') {
      caption = 'Show Slow Queries';
    } else if (actionType === 'DOWNLOAD_LOGS') {
      caption = 'Download Logs';
    } else if (actionType === 'HELM_OVERRIDES') {
      caption = 'View Kubernetes Overrides';
    }
    return caption;
  }

  static processName(dedicatedTo) {
    return dedicatedTo.charAt(0).toUpperCase() + dedicatedTo.toLowerCase().slice(1);
  }

  getLabel(actionType, dedicatedTo) {
    const btnLabel = NodeAction.getCaption(actionType, dedicatedTo);
    let btnIcon = null;
    if (actionType === 'STOP') {
      btnIcon = 'fa fa-stop-circle';
    } else if (actionType === 'REMOVE') {
      btnIcon = 'fa fa-minus-circle';
    } else if (actionType === 'DELETE') {
      btnIcon = 'fa fa-minus-circle';
    } else if (actionType === 'RELEASE') {
      btnIcon = 'fa fa-trash';
    } else if (actionType === 'REBOOT') {
      btnIcon = 'fa fa-repeat';
    } else if (actionType === 'START') {
      btnIcon = 'fa fa-play-circle';
    } else if (actionType === 'ADD') {
      btnIcon = 'fa fa-plus-circle';
    } else if (actionType === 'CONNECT') {
      btnIcon = 'fa fa-link';
    } else if (actionType === 'START_MASTER') {
      btnIcon = 'fa fa-play-circle';
    } else if (actionType === 'LIVE_QUERIES') {
      btnIcon = 'fa fa-search';
    } else if (actionType === 'SLOW_QUERIES') {
      btnIcon = 'fa fa-signal';
    } else if (actionType === 'DOWNLOAD_LOGS') {
      btnIcon = 'fa fa-download';
    } else if (actionType === 'HELM_OVERRIDES') {
      btnIcon = 'fa fa-eye';
    }
    return <YBLabelWithIcon icon={btnIcon}>{btnLabel}</YBLabelWithIcon>;
  }

  handleLiveQueryClick() {
    const { currentRow } = this.props;
    const path = browserHistory.getCurrentLocation().pathname;
    let universeUrl = '';
    if (path[path.length - 1] === '/') {
      universeUrl = path.substring(0, path.lastIndexOf('/', path.length - 2));
    } else {
      universeUrl = path.substring(0, path.lastIndexOf('/'));
    }
    browserHistory.push(`${universeUrl}/queries?nodeName=${currentRow.name}`);
  }

  handleSlowQueryClick() {
    const path = browserHistory.getCurrentLocation().pathname;
    let universeUrl = '';
    if (path[path.length - 1] === '/') {
      universeUrl = path.substring(0, path.lastIndexOf('/', path.length - 2));
    } else {
      universeUrl = path.substring(0, path.lastIndexOf('/'));
    }
    browserHistory.push(`${universeUrl}/queries?tab=slow-queries`);
  }

  render() {
    const {
      currentRow,
      universeUUID,
      providerUUID,
      hideConnect,
      hideQueries,
      disableStop,
      disableRemove,
      disabled,
      clusterType,
      isKubernetes,
      isOnPremManuallyProvisioned
    } = this.props;

    const allowedActions =
      isKubernetes || isOnPremManuallyProvisioned
        ? currentRow.allowedActions.filter((actionType) => actionType !== 'REBOOT')
        : currentRow.allowedActions;
    const actionButtons = allowedActions.map((actionType) => {
      const btnId = _.uniqueId('node_action_btn_');
      const isDisabled =
        disabled ||
        (actionType === 'STOP' && disableStop) ||
        (actionType === 'REMOVE' && disableRemove);

      if (actionType === 'QUERY') {
        if (!hideQueries) {
          return (
            <Fragment>
              <RbacValidator
                isControl
                accessRequiredOn={{
                  onResource: universeUUID,
                  ...UserPermissionMap.readUniverse
                }}
              >
                <MenuItem
                  key="live_queries_action_btn"
                  eventKey="live_queries_action_btn"
                  disabled={disabled}
                  onClick={this.handleLiveQueryClick}
                  data-testid={'NodeAction-LIVE_QUERIES'}
                >
                  {this.getLabel('LIVE_QUERIES', currentRow.dedicatedTo)}
                </MenuItem>
              </RbacValidator>
              <RbacValidator
                isControl
                accessRequiredOn={{
                  onResource: universeUUID,
                  ...UserPermissionMap.readUniverse
                }}
              >
                <MenuItem
                  key="slow_queries_action_btn"
                  eventKey="slow_queries_action_btn"
                  disabled={disabled}
                  onClick={this.handleSlowQueryClick}
                  data-testid={'NodeAction-SLOW_QUERIES'}
                >
                  {this.getLabel('SLOW_QUERIES', currentRow.dedicatedTo)}
                </MenuItem>
              </RbacValidator>
            </Fragment>
          );
        }
        return null;
      }
      if (!NodeAction.getCaption(actionType, currentRow.dedicatedTo)) return null;
      return (
        <RbacValidator
          isControl
          accessRequiredOn={{
            onResource: universeUUID,
            ...UserPermissionMap.editUniverse
          }}
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            key={`${btnId}${actionType}`}
            eventKey={`${btnId}`}
            disabled={isDisabled}
            onClick={() => isDisabled || this.openModal(actionType)}
            data-testid={`NodeAction-${actionType}`}
          >
            {this.getLabel(actionType, currentRow.dedicatedTo)}
          </MenuItem>
        </RbacValidator>
      );
    });

    // Add action to download master/tserver logs.
    const btnId = _.uniqueId('node_action_btn_');
    actionButtons.push(
      <RbacValidator
        isControl
        accessRequiredOn={{
          onResource: universeUUID,
          ...UserPermissionMap.editUniverse
        }}
      >
        <MenuItem
          key={btnId}
          eventKey={btnId}
          disabled={false}
          onClick={() => downloadLogs(universeUUID, currentRow.name)}
          data-testid={'NodeAction-DOWNLOAD_LOGS'}
        >
          {this.getLabel('DOWNLOAD_LOGS', currentRow.dedicatedTo)}
        </MenuItem>
      </RbacValidator>
    );

    if (isKubernetes) {
      const nodeOverridesID = _.uniqueId('k8s_override_action_');
      actionButtons.push(
        <RbacValidator
          isControl
          accessRequiredOn={{
            onResource: universeUUID,
            ...UserPermissionMap.editUniverse
          }}
        >
          <MenuItem
            key={nodeOverridesID}
            eventKey={nodeOverridesID}
            disabled={false}
            onClick={() => this.openOverridesModal(currentRow)}
            data-testid={'NodeAction-HELM_OVERRIDES'}
          >
            {this.getLabel('HELM_OVERRIDES', currentRow.dedicatedTo)}
          </MenuItem>
        </RbacValidator>
      );
    }

    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        {!hideConnect && (
          <NodeConnectModal
            currentRow={currentRow}
            providerUUID={providerUUID}
            label={this.getLabel('CONNECT', currentRow.dedicatedTo)}
            clusterType={clusterType}
            universeUUID={universeUUID}
          />
        )}
        {isNonEmptyArray(currentRow.allowedActions) ? (
          <Fragment>
            {actionButtons}
            <NodeActionModalContainer
              visible={this.state.showModal}
              onHide={this.closeModal}
              nodeInfo={currentRow}
              actionType={this.state.actionType}
            />
          </Fragment>
        ) : null}
        {this.state.overridesModal && (
          <NodeOverridesModal
            visible={this.state.overridesModal}
            onClose={this.closeOverridesModal}
            nodeId={this.state.currentNode}
            universeId={universeUUID}
          />
        )}
      </DropdownButton>
    );
  }
}

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
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { MenuItemsContainer } from '../UniverseDetail/compounds/MenuItemsContainer';
import { YBMenuItem } from '../UniverseDetail/compounds/YBMenuItem';

export default class NodeAction extends Component {
  constructor() {
    super();
    this.state = {
      showModal: false,
      actionType: null,
      overridesModal: false,
      currentNode: null,
      actionsDropdownOpen: false
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
    } else if (actionType === 'REPLACE') {
      caption = 'Replace Node';
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
    } else if (actionType === 'REPLACE') {
      btnIcon = 'fa fa-exchange';
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

  getPermission = (actionType, universeId) => {
    switch (actionType) {
      case 'CONNECT':
      case 'DOWNLOAD_LOGS':
      case 'LIVE_QUERIES':
      case 'SLOW_QUERIES':
        return hasNecessaryPerm({
          ...ApiPermissionMap.GET_UNIVERSES_BY_ID,
          onResource: { UNIVERSE: universeId }
        });
      default:
        return hasNecessaryPerm({
          ...ApiPermissionMap.MODIFY_UNIVERSE,
          onResource: { UNIVERSE: universeId }
        });
    }
  };

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
    const nodeAllowedActions = [];
    const advancedAllowedActions = [];

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
      isOnPremManuallyProvisioned,
      cluster,
      accessKeys
    } = this.props;

    const allowedActions =
      isKubernetes || isOnPremManuallyProvisioned
        ? currentRow.allowedActions.filter((actionType) => actionType !== 'REBOOT')
        : currentRow.allowedActions;

    allowedActions.forEach((action) => {
      if (action === 'REMOVE' || action === 'RELEASE' || action === 'ADD' || action === 'DELETE') {
        advancedAllowedActions.push(action);
      } else {
        nodeAllowedActions.push(action);
      }
    });

    const actionButtons = nodeAllowedActions.map((actionType) => {
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
                accessRequiredOn={{
                  ...ApiPermissionMap.GET_LIVE_QUERIES,
                  onResource: { UNIVERSE: universeUUID }
                }}
                overrideStyle={{ display: 'block' }}
                isControl
              >
                <YBMenuItem
                  key="live_queries_action_btn"
                  eventKey="live_queries_action_btn"
                  disabled={disabled}
                  onClick={this.handleLiveQueryClick}
                  dataTestId={'NodeAction-LIVE_QUERIES'}
                >
                  {this.getLabel('LIVE_QUERIES', currentRow.dedicatedTo)}
                </YBMenuItem>
              </RbacValidator>
              <RbacValidator
                accessRequiredOn={{
                  ...ApiPermissionMap.GET_SLOW_QUERIES,
                  onResource: { UNIVERSE: universeUUID }
                }}
                overrideStyle={{ display: 'block' }}
                isControl
              >
                <YBMenuItem
                  key="slow_queries_action_btn"
                  eventKey="slow_queries_action_btn"
                  disabled={disabled}
                  onClick={this.handleSlowQueryClick}
                  dataTestId={'NodeAction-SLOW_QUERIES'}
                >
                  {this.getLabel('SLOW_QUERIES', currentRow.dedicatedTo)}
                </YBMenuItem>
              </RbacValidator>
            </Fragment>
          );
        }
        return null;
      }
      if (!NodeAction.getCaption(actionType, currentRow.dedicatedTo)) return null;

      return (
        <RbacValidator
          customValidateFunction={() => this.getPermission(actionType, universeUUID)}
          overrideStyle={{ display: 'block' }}
          isControl
        >
          <YBMenuItem
            key={btnId}
            eventKey={btnId}
            disabled={isDisabled}
            onClick={() => isDisabled || this.openModal(actionType)}
            dataTestId={`NodeAction-${actionType}`}
          >
            {this.getLabel(actionType, currentRow.dedicatedTo)}
          </YBMenuItem>
        </RbacValidator>
      );
    });

    const advancedActionButtons = advancedAllowedActions.map((advancedActionType) => {
      const btnId = _.uniqueId('node_action_btn_');
      const isDisabled =
        disabled ||
        (advancedActionType === 'STOP' && disableStop) ||
        (advancedActionType === 'REMOVE' && disableRemove);

      if (!NodeAction.getCaption(advancedActionType, currentRow.dedicatedTo)) return null;

      return (
        <RbacValidator
          customValidateFunction={() => this.getPermission(advancedActionType, universeUUID)}
          overrideStyle={{ display: 'block' }}
          isControl
        >
          <YBMenuItem
            key={btnId}
            eventKey={btnId}
            disabled={isDisabled}
            onClick={() => isDisabled || this.openModal(advancedActionType)}
            dataTestId={`NodeAction-${advancedActionType}`}
          >
            {this.getLabel(advancedActionType, currentRow.dedicatedTo)}
          </YBMenuItem>
        </RbacValidator>
      );
    });

    // Add action to download master/tserver logs.
    const btnId = _.uniqueId('node_action_btn_');
    actionButtons.push(
      <RbacValidator
        accessRequiredOn={{
          ...ApiPermissionMap.DOWNLOAD_UNIVERSE_NODE_LOGS,
          onResource: { UNIVERSE: universeUUID }
        }}
        overrideStyle={{ display: 'block' }}
        isControl
      >
        <YBMenuItem
          key={btnId}
          eventKey={btnId}
          disabled={false}
          onClick={() => downloadLogs(universeUUID, currentRow.name)}
          dataTestId={'NodeAction-DOWNLOAD_LOGS'}
        >
          {this.getLabel('DOWNLOAD_LOGS', currentRow.dedicatedTo)}
        </YBMenuItem>
      </RbacValidator>
    );

    if (isKubernetes) {
      const nodeOverridesID = _.uniqueId('k8s_override_action_');
      actionButtons.push(
        <RbacValidator
          accessRequiredOn={{
            ...ApiPermissionMap.MODIFY_UNIVERSE,
            onResource: { UNIVERSE: universeUUID }
          }}
          overrideStyle={{ display: 'block' }}
          isControl
        >
          <YBMenuItem
            key={nodeOverridesID}
            eventKey={nodeOverridesID}
            disabled={false}
            onClick={() => this.openOverridesModal(currentRow)}
            dataTestId={'NodeAction-HELM_OVERRIDES'}
          >
            {this.getLabel('HELM_OVERRIDES', currentRow.dedicatedTo)}
          </YBMenuItem>
        </RbacValidator>
      );
    }

    const accessKeyCode = cluster.userIntent.accessKeyCode;
    const accessKey = accessKeys.data.find(
      (key) => key.idKey.providerUUID === providerUUID && key.idKey.keyCode === accessKeyCode
    );
    return (
      <DropdownButton
        className="btn btn-default"
        title="Actions"
        id="bg-nested-dropdown"
        onToggle={(isOpen) => this.setState({ actionsDropdownOpen: isOpen })}
        pullRight
      >
        <MenuItemsContainer
          parentDropdownOpen={this.state.actionsDropdownOpen}
          mainMenu={(showSubmenu) => (
            <>
              {!hideConnect && (
                <NodeConnectModal
                  currentRow={currentRow}
                  providerUUID={providerUUID}
                  label={this.getLabel('CONNECT', currentRow.dedicatedTo)}
                  clusterType={clusterType}
                  universeUUID={universeUUID}
                  disabled={accessKey === undefined}
                />
              )}
              {isNonEmptyArray(nodeAllowedActions) ? (
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
              {isNonEmptyArray(advancedAllowedActions) && (
                <RbacValidator
                  customValidateFunction={() =>
                    this.getPermission(this.state.actionType, universeUUID)
                  }
                  overrideStyle={{ display: 'block' }}
                  isControl
                >
                  <YBMenuItem
                    key={'node_action_btn_advanced'}
                    onClick={() => showSubmenu('advanced')}
                    dataTestId={`NodeAction-ADVANCED`}
                  >
                    <YBLabelWithIcon icon="fa fa-cog">Advanced</YBLabelWithIcon>

                    <span className="pull-right">
                      <i className="fa fa-chevron-right submenu-icon" />
                    </span>
                  </YBMenuItem>
                </RbacValidator>
              )}
              {this.state.overridesModal && (
                <NodeOverridesModal
                  visible={this.state.overridesModal}
                  onClose={this.closeOverridesModal}
                  nodeId={this.state.currentNode}
                  universeId={universeUUID}
                />
              )}
            </>
          )}
          subMenus={{
            advanced: (backToMainMenu) => (
              <>
                <MenuItem onClick={backToMainMenu}>
                  <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">Back</YBLabelWithIcon>
                </MenuItem>
                <MenuItem divider />
                {advancedActionButtons}
                <NodeActionModalContainer
                  visible={this.state.showModal}
                  onHide={this.closeModal}
                  nodeInfo={currentRow}
                  actionType={this.state.actionType}
                />
              </>
            )
          }}
        />
      </DropdownButton>
    );
  }
}

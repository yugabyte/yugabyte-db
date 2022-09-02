// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { browserHistory } from 'react-router';
import PropTypes from 'prop-types';
import { NodeActionModalContainer, NodeConnectModal } from '../../universes';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { downloadLogs } from '../../../actions/universe';

import _ from 'lodash';

export default class NodeAction extends Component {
  constructor(props) {
    super();
    this.state = {
      showModal: false,
      actionType: null
    };
    this.closeModal = this.closeModal.bind(this);
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

  static getCaption(actionType) {
    let caption = null;
    if (actionType === 'STOP') {
      caption = 'Stop Processes';
    } else if (actionType === 'REMOVE') {
      caption = 'Remove Node';
    } else if (actionType === 'DELETE') {
      caption = 'Delete Node';
    } else if (actionType === 'RELEASE') {
      caption = 'Release Instance';
    } else if (actionType === 'REBOOT') {
      caption = 'Reboot Node';
    } else if (actionType === 'START') {
      caption = 'Start Processes';
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
    }
    return caption;
  }

  getLabel(actionType) {
    const btnLabel = NodeAction.getCaption(actionType);
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
      disabled
    } = this.props;
    const actionButtons = currentRow.allowedActions.map((actionType, idx) => {
      const btnId = _.uniqueId('node_action_btn_');
      const isDisabled =
        disabled ||
        (actionType === 'STOP' && disableStop) ||
        (actionType === 'REMOVE' && disableRemove);
      if (actionType === 'QUERY') {
        if (!hideQueries) {
          return (
            <Fragment>
              <MenuItem key="live_queries_action_btn" eventKey="live_queries_action_btn"
                disabled={disabled} onClick={this.handleLiveQueryClick}>
                {this.getLabel('LIVE_QUERIES')}
              </MenuItem>
              <MenuItem key="slow_queries_action_btn" eventKey="slow_queries_action_btn"
                disabled={disabled} onClick={this.handleSlowQueryClick}>
                {this.getLabel('SLOW_QUERIES')}
              </MenuItem>
            </Fragment>
          );
        }
        return null;
      }
      return (
        <MenuItem
          key={btnId}
          eventKey={btnId}
          disabled={isDisabled}
          onClick={() => isDisabled || this.openModal(actionType)}
        >
          {this.getLabel(actionType)}
        </MenuItem>
      );
    });

    // Add action to download master/tserver logs.
    const btnId = _.uniqueId('node_action_btn_');
    actionButtons.push(
      (
        <MenuItem
          key={btnId}
          eventKey={btnId}
          disabled={false}
          onClick={() => downloadLogs(universeUUID, currentRow.name)}
        >
          {this.getLabel('DOWNLOAD_LOGS')}
        </MenuItem>
      )
    );

    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        {!hideConnect && (
          <NodeConnectModal
            currentRow={currentRow}
            providerUUID={providerUUID}
            label={this.getLabel('CONNECT')}
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
      </DropdownButton>
    );
  }
}

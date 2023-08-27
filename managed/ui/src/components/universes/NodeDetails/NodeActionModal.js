// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import PropTypes from 'prop-types';
import { browserHistory } from 'react-router';
import { NodeAction } from '../../universes';

const nodeActionExpectedResult = {
  START: 'Live',
  STOP: 'Stopped',
  REMOVE: 'Removed',
  RELEASE: 'Unreachable',
  DELETE: 'Unreachable'
};
export default class NodeActionModal extends Component {
  static propTypes = {
    nodeInfo: PropTypes.object.isRequired,
    actionType: PropTypes.string
  };

  pollNodeStatusUpdate = (universeUUID, actionType, nodeName, payload) => {
    const { getNodeDetails, getNodeDetailsResponse } = this.props;
    this.interval = setTimeout(() => {
      getNodeDetails(universeUUID, nodeName).then((response) => {
        if (response.payload?.data) {
          const node = response.payload.data;
          if (actionType === 'DELETE' || node.state === nodeActionExpectedResult[actionType]) {
            clearInterval(this.interval);
            getNodeDetailsResponse(response.payload);
            return;
          }
          getNodeDetailsResponse(response.payload);
          this.pollNodeStatusUpdate(universeUUID, actionType, nodeName, payload);
        }
      });
    }, 1500);
  };

  componentWillUnmount() {
    if (this.interval) {
      clearInterval(this.interval);
    }
  }

  performNodeAction = () => {
    const {
      universe: { currentUniverse },
      nodeInfo,
      actionType,
      performUniverseNodeAction,
      onHide
    } = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    performUniverseNodeAction(universeUUID, nodeInfo.name, actionType).then((response) => {
      if (response.error !== true) {
        this.pollNodeStatusUpdate(universeUUID, actionType, nodeInfo.name, response.payload);
      }
    });
    onHide();
    browserHistory.push('/universes/' + universeUUID + '/nodes');
  };

  render() {
    const { visible, onHide, nodeInfo, actionType } = this.props;
    if (actionType === null || nodeInfo === null) {
      return <span />;
    }

    return (
      <div className="universe-apps-modal">
        <YBModal
          title={`Perform Node Action: ${NodeAction.getCaption(actionType)} `}
          visible={visible}
          onHide={onHide}
          showCancelButton={true}
          cancelLabel={'Cancel'}
          onFormSubmit={this.performNodeAction}
        >
          Are you sure you want to {actionType.toLowerCase()} {nodeInfo.name}?
        </YBModal>
      </div>
    );
  }
}

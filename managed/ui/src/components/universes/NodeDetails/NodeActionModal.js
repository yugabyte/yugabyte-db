// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import PropTypes from 'prop-types';
import _ from 'lodash';

export default class NodeActionModal extends Component {
  static propTypes = {
    nodeInfo: PropTypes.object.isRequired,
    actionType: PropTypes.string
  }

  performNodeAction = () => {
    const {
      universe: {currentUniverse} ,
      nodeInfo,
      actionType,
      performUniverseNodeAction,
      onHide
    } = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    console.log(universeUUID, nodeInfo.name, actionType);
    performUniverseNodeAction(universeUUID, nodeInfo.name, actionType);
    onHide();
  };

  render() {
    const { visible, onHide, nodeInfo, actionType } = this.props;
    if (actionType === null || nodeInfo === null) {
      return <span />;
    }

    return (
      <div className="universe-apps-modal">
        <YBModal title={`Perform Node Action: ${_.capitalize(actionType.toLowerCase())} `}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={this.performNodeAction}>
          Are you sure you want to {actionType.toLowerCase()} {nodeInfo.name}?
        </YBModal>
      </div>
    );
  }
}

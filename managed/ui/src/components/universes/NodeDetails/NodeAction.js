// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { NodeActionModalContainer, NodeConnectModal } from '../../universes';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';

import _ from 'lodash';

export default class NodeAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false,
      actionType: null
    };
    this.closeModal = this.closeModal.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    actionType: PropTypes.oneOf(['STOP', 'REMOVE'])
  };

  openModal(actionType) {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.row,
        actionType: actionType,
        showModal: true
      };
    });
  }

  closeModal() {
    this.setState((prevState, props) => {
      return {
        showModal: false
      };
    });
  }

  static getCaption(actionType) {
    let caption = null;
    if (actionType === "STOP") {
      caption = "Stop Processes";
    } else if (actionType === "REMOVE") {
      caption = "Remove Node";
    } else if (actionType === "DELETE") {
      caption = "Delete Node";
    } else if (actionType === "RELEASE") {
      caption = "Release Instance";
    } else if (actionType === "START") {
      caption = "Start Processes";
    } else if (actionType === "ADD") {
      caption = "Add Node";
    } else if (actionType === "CONNECT") {
      caption = "Connect";
    } else if (actionType === "START_MASTER") {
      caption = "Start Master";
    }
    return caption;
  }

  getLabel(actionType) {
    const btnLabel = NodeAction.getCaption(actionType);
    let btnIcon = null;
    if (actionType === "STOP") {
      btnIcon = "fa fa-stop-circle";
    } else if (actionType === "REMOVE") {
      btnIcon = "fa fa-minus-circle";
    } else if (actionType === "DELETE") {
      btnIcon = "fa fa-minus-circle";
    } else if (actionType === "RELEASE") {
      btnIcon = "fa fa-trash";
    } else if (actionType === "START") {
      btnIcon = "fa fa-play-circle";
    } else if (actionType === "ADD") {
      btnIcon = "fa fa-plus-circle";
    } else if (actionType === "CONNECT") {
      btnIcon = "fa fa-link";
    } else if (actionType === "START_MASTER") {
      btnIcon = "fa fa-play-circle";
    }

    return (
      <YBLabelWithIcon icon={btnIcon}>
        {btnLabel}
      </YBLabelWithIcon>
    );
  }

  render() {
    const { currentRow, providerUUID, disableConnect, disabled } = this.props;
    const actionButtons = currentRow.allowedActions.map((actionType, idx) => {
      const btnId = _.uniqueId('node_action_btn_');
      return (
        <MenuItem key={btnId} eventKey={btnId} disabled={disabled}
                  onClick={disabled ? null : this.openModal.bind(this, actionType)}>
          {this.getLabel(actionType)}
        </MenuItem>
      );
    });

    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        {!disableConnect && <NodeConnectModal currentRow={currentRow}
          providerUUID={providerUUID} label={this.getLabel("CONNECT")}/>}
        {
          isNonEmptyArray(currentRow.allowedActions)
            ? <Fragment>{actionButtons}
              <NodeActionModalContainer
                visible = {this.state.showModal}
                onHide = { this.closeModal}
                nodeInfo = {currentRow}
                actionType = {this.state.actionType}
              />
            </Fragment>
          : null
        }
      </DropdownButton>
    );
  }
}

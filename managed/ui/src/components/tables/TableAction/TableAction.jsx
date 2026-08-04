// Copyright (c) YugabyteDB, Inc.

import { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { BulkImportContainer } from '../../../components/tables';
import { MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBButton } from '../../common/forms/fields';
import _ from 'lodash';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

export default class TableAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false
    };
    this.openModal = this.openModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    isMenuItem: PropTypes.bool,
    btnClass: PropTypes.string,
    onModalSubmit: PropTypes.func,
    onSubmit: PropTypes.func,
    onError: PropTypes.func,
    actionType: PropTypes.oneOf([
      'drop',
      'import',
      'create-backup',
      'create-scheduled-backup',
      'restore-backup',
      'delete-backup'
    ])
  };

  static defaultProps = {
    isMenuItem: true,
    btnClass: 'btn-default'
  };

  openModal() {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.currentRow,
        showModal: true
      };
    });
  }

  closeModal() {
    this.setState(() => {
      return {
        showModal: false
      };
    });
  }

  render() {
    const { actionType, isMenuItem, disabled, onSubmit, onError } = this.props;
    let modalContainer = null;
    let btnLabel = null;
    let btnIcon = null;
    if (actionType === 'import') {
      btnLabel = 'Bulk Import';
      btnIcon = 'fa fa-download';
      modalContainer = (
        <BulkImportContainer
          visible={this.state.showModal}
          onHide={this.closeModal}
          tableInfo={this.state.selectedRow}
        />
      );
    }

    const getRbacWrappedComp = (hasPerm) => {
      return (
        <RbacValidator
          customValidateFunction={() => hasPerm}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <Fragment>
            <MenuItem
              eventKey={btnId}
              onClick={!hasPerm ? null : this.openModal}
              disabled={!hasPerm}
            >
              <YBLabelWithIcon icon={btnIcon}>{btnLabel}</YBLabelWithIcon>
            </MenuItem>
            {modalContainer}
          </Fragment>
        </RbacValidator>
      );
    };

    const btnId = _.uniqueId('table_action_btn_');

    if (actionType === 'import') {
      return getRbacWrappedComp(
        hasNecessaryPerm({
          ...ApiPermissionMap.BULK_IMPORT_TABLES,
          onResource: this.props.universeUUID
        })
      );
    }

    if (isMenuItem) {
      return (
        <Fragment>
          <MenuItem eventKey={btnId} onClick={disabled ? null : this.openModal} disabled={disabled}>
            <YBLabelWithIcon icon={btnIcon}>{btnLabel}</YBLabelWithIcon>
          </MenuItem>
          {modalContainer}
        </Fragment>
      );
    }
    return (
      <div className={this.props.className}>
        <YBButton
          btnText={btnLabel}
          btnIcon={btnIcon}
          disabled={disabled}
          btnClass={'btn ' + this.props.btnClass}
          onClick={disabled ? null : this.openModal}
        />
        {modalContainer}
      </div>
    );
  }
}

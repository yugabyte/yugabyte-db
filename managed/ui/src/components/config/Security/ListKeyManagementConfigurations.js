// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Button } from 'react-bootstrap';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { YBConfirmModal } from '../../modals';

class ListKeyManagementConfiguration extends Component {
  state = {
    visibleModal: '',
  }

  render() {
    const { configs, onCreate, onDelete } = this.props;

    const actionList = (item, row) => {
      const kmsProvider = row.provider;
      return (
        <Button
          title={"Delete provider"}
          bsClass="btn btn-default btn-config pull-right"
          onClick={() => {
            if (kmsProvider === 'AWS') {
              this.setState({ visibleModal: 'confirmDeleteKMSConfig' });
            } else {
              onDelete(kmsProvider);
            }}}>
          Delete Configuration
        </Button>
      );
    };

    const showConfigProperties = (item, row) => {
      const displayed = [];
      Object.keys(row).forEach(key => {
        if (key !== 'provider') {
          displayed.push(`${key}: ${row[key]}`);
        }
      });
      return (
        <div>
          <a onClick={(e) => {
            e.target.classList.add("yb-hidden");
            e.target.parentNode.lastChild.classList.remove("yb-hidden");
          }}>Show details</a>
          <span title={displayed.join(', ')} className="yb-hidden">{displayed.join(', ')}</span>
        </div>
      );
    };

    return (
      <div>
        <YBPanelItem
          header={
            <Fragment>
              <h2 className="table-container-title pull-left">List Configurations</h2>
              <FlexContainer className="pull-right">
                <FlexShrink>
                  <Button bsClass="btn btn-orange btn-config" onClick={onCreate}>
                    Create New Config
                  </Button>
                </FlexShrink>
              </FlexContainer>
            </Fragment>
          }
          body={
            <Fragment>
              <BootstrapTable data={configs.data} className="backup-list-table middle-aligned-table">
                <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
                <TableHeaderColumn dataField="provider" dataSort
                      columnClassName="no-border name-column" className="no-border">
                  Provider
                </TableHeaderColumn>
                <TableHeaderColumn dataField="base_url" dataFormat={showConfigProperties}
                      columnClassName="no-border name-column" className="no-border">
                  Properties
                </TableHeaderColumn>
                <TableHeaderColumn dataField="configActions" dataFormat={actionList}
                                  columnClassName="no-border name-column no-side-padding" className="no-border">
                </TableHeaderColumn>
              </BootstrapTable>
            </Fragment>
          }
          noBackground
          />
        <YBConfirmModal name={"confirmDeleteKMSConfig"} title={"Delete Configuration"}
                        hideConfirmModal={() => this.setState({ visibleModal: '' })}
                        currentModal={"confirmDeleteKMSConfig"} visibleModal={this.state.visibleModal}
                        onConfirm={() => onDelete('AWS')} confirmLabel={"Delete"}
                        cancelLabel={"Cancel"}>
          <p>This configuration and the original key will be deleted and is unrecoverable.</p>
          <p>Are you sure you want to delete this AWS KMS configuration?</p>
        </YBConfirmModal>
      </div>
    );
  }
}
export default ListKeyManagementConfiguration;

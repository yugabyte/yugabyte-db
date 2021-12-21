// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { ConfigDetails } from './ConfigDetails';
import { AssociatedUniverse } from '../../common/associatedUniverse/AssociatedUniverse';

export class ListKeyManagementConfigurations extends Component {
  state = {
    associatedUniverses: [],
    isVisibleModal: false,
    configDetail: null
  };

  actionList = (item, row) => {
    const { configUUID, in_use, universeDetails } = row.metadata;
    const { isAdmin, onDelete, onEdit } = this.props;
    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        <MenuItem
          onClick={() => {
            this.setState({ configDetail: row });
          }}
        >
          <i className="fa fa-info-circle"></i> Details
        </MenuItem>
        {isAdmin && (
          <MenuItem onClick={() => onEdit(row)}>
            <i className="fa fa-pencil"></i> Edit Configuration
          </MenuItem>
        )}
        <MenuItem
          title={'Delete provider'}
          disabled={in_use}
          onClick={() => {
            !in_use && onDelete(configUUID);
          }}
        >
          <i className="fa fa-trash"></i> Delete Configuration
        </MenuItem>
        <MenuItem
          onClick={() => {
            this.setState({ associatedUniverses: [...universeDetails], isVisibleModal: true });
          }}
        >
          <i className="fa fa-eye"></i> Show Universes
        </MenuItem>
      </DropdownButton>
    );
  };

  /**
   * Close the modal by setting the local flag
   */
  closeModal = () => {
    this.setState({ isVisibleModal: false });
  };

  hideConfigDetail = () => {
    this.setState({ configDetail: null });
  };

  render() {
    const { configs, onCreate } = this.props;

    const { associatedUniverses, isVisibleModal, configDetail } = this.state;

    const showConfigProperties = (item, row) => {
      return (
        <div>
          <a
            className="show_details"
            onClick={(e) => {
              this.setState({ configDetail: row });
              e.preventDefault();
            }}
            href="/"
          >
            Show details
          </a>
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
              <BootstrapTable
                data={configs.data}
                className="backup-list-table middle-aligned-table"
              >
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.configUUID}
                  isKey={true}
                  hidden={true}
                />
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.name}
                  dataSort
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.provider}
                  dataSort
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Provider
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="base_url"
                  dataFormat={showConfigProperties}
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Properties
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="configActions"
                  dataFormat={this.actionList}
                  width="120px"
                  columnClassName="yb-actions-cell"
                />
              </BootstrapTable>
              <AssociatedUniverse
                visible={isVisibleModal}
                onHide={this.closeModal}
                associatedUniverses={associatedUniverses}
                title="KMS Provider"
              />
            </Fragment>
          }
          noBackground
        />
        {configDetail && (
          <ConfigDetails
            visible={!!configDetail}
            onHide={this.hideConfigDetail}
            data={configDetail}
          />
        )}
      </div>
    );
  }
}

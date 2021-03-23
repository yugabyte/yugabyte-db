// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { AssociatedUniverse } from '../../common/associatedUniverse/AssociatedUniverse';

export class ListKeyManagementConfigurations extends Component {
  state = {
    associatedUniverses: [],
    isVisibleModal: false
  };

  actionList = (item, row) => {
    const { configUUID, in_use, universeDetails } = row.metadata;
    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        <MenuItem
          title={'Delete provider'}
          disabled={in_use}
          onClick={() => {
            !in_use && this.props.onDelete(configUUID);
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

  render() {
    const {
      configs,
      onCreate,
    } = this.props;

    const { associatedUniverses, isVisibleModal } = this.state;

    const showConfigProperties = (item, row) => {
      const displayed = [];
      const credentials = row.credentials;
      Object.keys(credentials).forEach((key) => {
        if (key !== 'provider') {
          displayed.push(`${key}: ${credentials[key]}`);
        }
      });
      return (
        <div>
          <a
            onClick={(e) => {
              e.target.classList.add('yb-hidden');
              e.target.parentNode.lastChild.classList.remove('yb-hidden');
              e.preventDefault();
            }}
            href="/"
          >
            Show details
          </a>
          <span title={displayed.join(', ')} className="yb-hidden">
            {displayed.join(', ')}
          </span>
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
      </div>
    );
  }
}

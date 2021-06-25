// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the configuration list of alerts.

import React from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBPanelItem } from '../../panels';

/**
 * This is the header for YB Panel Item.
 */
const header = () => (
  <>
    <h2 className="table-container-title pull-left">Alert Configurations</h2>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <DropdownButton
          className="backup-config-actions btn btn-default"
          title="Create Alert"
          id="bg-nested-dropdown"
          pullRight
        >
          <MenuItem>
            <i className="fa fa-pencil"></i> Universe Alert
          </MenuItem>

          <MenuItem>
            <i className="fa fa-trash"></i> Platform Alert
          </MenuItem>
        </DropdownButton>
      </FlexShrink>
    </FlexContainer>
  </>
);

export const AlertsList = (props) => {
  const { data } = props;

  // This method will handle all the required actions for the particular row.
  const formatConfigActions = (cell, row) => {
    return (
      <>
        <DropdownButton
          className="backup-config-actions btn btn-default"
          title="Actions"
          id="bg-nested-dropdown"
          pullRight
        >
          <MenuItem>
            <i className="fa fa-pencil"></i> Edit Alert
          </MenuItem>

          <MenuItem>
            <i className="fa fa-trash"></i> Delete Alert
          </MenuItem>
        </DropdownButton>
      </>
    );
  };

  // TODO: This needs to be updated once the real data will come.
  // For now, we're dealing with the mock data.
  return (
    <YBPanelItem
      header={header()}
      body={
        <>
          <BootstrapTable className="backup-list-table middle-aligned-table" data={data}>
            <TableHeaderColumn dataField="configUUID" isKey={true} hidden={true} />
            <TableHeaderColumn
              dataField="configName"
              columnClassName="no-border name-column"
              className="no-border"
            >
              Name
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="type"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Type
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="severity"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Severity
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="destination"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Destinations
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="created"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Created
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="occurance"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Occurance
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="status"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Status
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="configActions"
              dataFormat={(cell, row) => formatConfigActions(cell, row)}
              columnClassName="yb-actions-cell"
              className="yb-actions-cell"
            >
              Actions
            </TableHeaderColumn>
          </BootstrapTable>
        </>
      }
      noBackground
    />
  );
};

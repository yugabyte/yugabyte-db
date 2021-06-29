// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the destination list of alerts.

import React from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBPanelItem } from '../../panels';

/**
 * This is the header for YB Panel Item.
 */
const header = () => (
  <>
    <h2 className="table-container-title pull-left">Alert Destinations</h2>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <Button bsClass="alert-config-actions btn btn-orange btn-config">Add Destination</Button>
      </FlexShrink>
    </FlexContainer>
  </>
);

export const AlertDestionations = (props) => {
  const {
    data: { payload }
  } = props;

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
            <i className="fa fa-pencil"></i> Edit Destination
          </MenuItem>

          <MenuItem>
            <i className="fa fa-trash"></i> Delete Destination
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
          <BootstrapTable className="backup-list-table middle-aligned-table" data={payload}>
            <TableHeaderColumn dataField="UUID" isKey={true} hidden={true} />
            <TableHeaderColumn
              dataField="destinations"
              columnClassName="no-border name-column"
              className="no-border"
            >
              Destinations
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="channels"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Channels
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

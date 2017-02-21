// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { isValidObject } from '../../../utils/ObjectUtils';
import { DescriptionList } from '../../common/descriptors';

export default class TableInfoPanel extends Component {
  static propTypes = {
    tableInfo: PropTypes.object.isRequired
  };

  render() {
    const {tableInfo} = this.props;
    var tableInfoItems = [
      { name: "Table Name", data: isValidObject(tableInfo.tableDetails) ? tableInfo.tableDetails.tableName : ""},
      { name: "Table Type", data: tableInfo.tableType},
      { name: "Table UUID", data: tableInfo.tableUUID}
    ]

    return (
      <DescriptionList listItems={tableInfoItems} />
    );
  }
}

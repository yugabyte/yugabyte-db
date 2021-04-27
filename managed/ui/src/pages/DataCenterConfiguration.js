// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import DataCenterConfigurationContainer from '../components/config/ConfigProvider/DataCenterConfigurationContainer';

class DataCenterConfiguration extends Component {
  render() {
    return (
      <div>
        <DataCenterConfigurationContainer {...this.props} />
      </div>
    );
  }
}
export default DataCenterConfiguration;

// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { connect } from 'react-redux';

import { YBButton } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

class UniverseAction extends Component {
  render() {
    return (
      <div>
        {(
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.CREATE_MAINTENANCE_WINDOW
            }}
            isControl
          >
            <YBButton
              btnText="Create Maintenance Window"
              btnIcon="fa fa-clock-o"
              btnClass={`btn btn-orange`}
              onClick={() => window.open(`/admin/alertConfig/maintenanceWindow`, '_blank')}
              style={{ marginRight: '5px' }}
            />
            <YBInfoTip
                title="Create Maintenance Window"
                content="Create a maintenance window to snooze alerts on your universe."
                placement="left"
              />
          </RbacValidator>
        )}
      </div>
    );
  }
}

export default UniverseAction;

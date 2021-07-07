// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nissharma@deloitte.com)
// 
// This file will hold all the details related to the
// channels for the respective destination.
// TODO: API needs to return the data in a proper format and then
// we gonna design the component as per the expectation.

import React from 'react';
import { YBModal } from '../../common/forms/fields';

export const AlertDestinationDetails = ({ details, visible, onHide }) => {
  return (
    <div className="cert-details-modal">
      <YBModal
        title="Alert Destination Details"
        visible={visible}
        onHide={onHide}
        submitLabel={'Close'}
        onFormSubmit={onHide}
      >
        ...
      </YBModal>
    </div>
  );
};

// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nissharma@deloitte.com)
// 

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

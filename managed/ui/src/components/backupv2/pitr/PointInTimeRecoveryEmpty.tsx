/*
 * Created on Tue Jun 07 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { YBButton } from '../../common/forms/fields';
import { BackupEmpty } from '../components/BackupEmpty';

const REPEAT_ICON = <i className="fa fa-repeat fa-flip-horizontal backup-empty-icon" />;

export const PointInTimeRecoveryEmpty = ({
  onActionButtonClick,
  disabled = false
}: {
  onActionButtonClick: Function;
  disabled?: boolean;
}) => {
  return (
    <BackupEmpty classNames="point-in-time-recovery">
      {REPEAT_ICON}
      <RbacValidator
        customValidateFunction={() => !disabled}
        isControl
      >
        <YBButton
          onClick={onActionButtonClick}
          btnClass="btn btn-orange backup-empty-button"
          btnText="Enable Point-In-Time Recovery"
        />
      </RbacValidator>
      <div className="sub-text">
        Currently there are no databases with Point-In-Time Recovery enabled
      </div>
    </BackupEmpty>
  );
};

/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';

import { YBButton } from '../../../redesign/components';

import styles from './EmptyListPlaceholder.module.scss';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';

interface EmptyListPlaceholderProps {
  actionButtonText: string;
  descriptionText: string;
  onActionButtonClick: () => void;

  className?: string;
  dataTestIdPrefix?: string;
}

const PLUS_ICON = <i className={`fa fa-plus ${styles.emptyIcon}`} />;

export const EmptyListPlaceholder = ({
  actionButtonText,
  className,
  descriptionText,
  dataTestIdPrefix,
  onActionButtonClick
}: EmptyListPlaceholderProps) => (

  <div className={clsx(styles.emptyListContainer, className)}>
    {PLUS_ICON}
    <RbacValidator
      accessRequiredOn={{
        onResource: "CUSTOMER_ID",
        ...UserPermissionMap.createProvider
      }}
      isControl
    >
      <YBButton
        style={{ minWidth: '200px' }}
        variant="primary"
        onClick={onActionButtonClick}
        data-testid={`${dataTestIdPrefix ?? 'EmptyListPlaceholder'}-PrimaryAction`}
      >
        <i className="fa fa-plus" />
        {actionButtonText}
      </YBButton>
    </RbacValidator>
    <div>{descriptionText}</div>
  </div>
);

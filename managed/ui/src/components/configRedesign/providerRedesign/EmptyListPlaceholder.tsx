/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';

import { YBButton } from '../../../redesign/components';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionProps } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

import styles from './EmptyListPlaceholder.module.scss';

interface EmptyListPlaceholderProps {
  actionButtonText: string;
  accessRequiredOn: ApiPermissionProps;
  descriptionText: string;
  onActionButtonClick: () => void;
  variant: 'primary' | 'secondary';

  className?: string;
  dataTestIdPrefix?: string;
  isDisabled?: boolean;
}

const PLUS_ICON = <i className={`fa fa-plus ${styles.emptyIcon}`} />;

export const EmptyListPlaceholder = ({
  actionButtonText,
  accessRequiredOn,
  className,
  descriptionText,
  dataTestIdPrefix,
  variant,
  onActionButtonClick,
  isDisabled
}: EmptyListPlaceholderProps) => (
  <div className={clsx(styles.emptyListContainer, className)}>
    {variant === 'primary' && PLUS_ICON}
    <div>{descriptionText}</div>
    <RbacValidator accessRequiredOn={accessRequiredOn} isControl>
      <YBButton
        style={{ minWidth: '200px' }}
        variant={variant}
        onClick={onActionButtonClick}
        disabled={isDisabled}
        data-testid={`${dataTestIdPrefix ?? 'EmptyListPlaceholder'}-PrimaryAction`}
      >
        <i className="fa fa-plus" />
        {actionButtonText}
      </YBButton>
    </RbacValidator>
  </div>
);

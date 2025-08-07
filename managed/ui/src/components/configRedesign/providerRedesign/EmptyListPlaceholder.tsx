/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ReactNode } from 'react';
import clsx from 'clsx';

import { YBButton } from '../../../redesign/components';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionProps } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

import styles from './EmptyListPlaceholder.module.scss';

interface EmptyListPlaceholderCommonProps {
  descriptionText: string;
  variant: 'primary' | 'secondary';

  className?: string;
}
type EmptyListPlaceholderProps =
  | (EmptyListPlaceholderCommonProps & {
      isCustomPrimaryAction: false;
      actionButtonText: string;
      accessRequiredOn: ApiPermissionProps;
      descriptionText: string;
      onActionButtonClick: () => void;
      dataTestIdPrefix?: string;
      isDisabled?: boolean;
    })
  | (EmptyListPlaceholderCommonProps & {
      isCustomPrimaryAction: true;
      customPrimaryAction: ReactNode;
    });
const PLUS_ICON = <i className={`fa fa-plus ${styles.emptyIcon}`} />;

export const EmptyListPlaceholder = (props: EmptyListPlaceholderProps) => {
  const { className, variant, descriptionText } = props;

  return (
    <div className={clsx(styles.emptyListContainer, className)}>
      {variant === 'primary' && PLUS_ICON}
      <div>{descriptionText}</div>
      {props.isCustomPrimaryAction === true ? (
        props.customPrimaryAction
      ) : (
        <RbacValidator accessRequiredOn={props.accessRequiredOn} isControl>
          <YBButton
            style={{ minWidth: '200px' }}
            variant={variant}
            onClick={props.onActionButtonClick}
            disabled={props.isDisabled}
            data-testid={`${props.dataTestIdPrefix ?? 'EmptyListPlaceholder'}-PrimaryAction`}
          >
            <i className="fa fa-plus" />
            {props.actionButtonText}
          </YBButton>
        </RbacValidator>
      )}
    </div>
  );
};

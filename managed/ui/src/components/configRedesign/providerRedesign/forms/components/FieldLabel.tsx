/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ReactNode } from 'react';
import clsx from 'clsx';

import { YBLabel } from '../../../../../redesign/components';
import YBInfoTip from '../../../../common/descriptors/YBInfoTip';

import styles from './FieldLabel.module.scss';

interface FieldLabelProps {
  children?: ReactNode;
  className?: string;
  infoContent?: string;
  infoTitle?: string;
}

export const FieldLabel = ({ className, children, infoContent, infoTitle }: FieldLabelProps) => {
  return (
    <YBLabel className={clsx(styles.fieldLabel, className)}>
      <span>
        {children}
        {infoContent && (
          <>
            &nbsp;
            <YBInfoTip content={infoContent} title={infoTitle} />
          </>
        )}
      </span>
    </YBLabel>
  );
};

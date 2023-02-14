/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { Typography } from '@material-ui/core';

import styles from './FieldGroup.module.scss';
import YBInfoTip from '../../../../common/descriptors/YBInfoTip';

interface FieldGroupProps {
  heading: string;
  headerAccessories?: React.ReactNode;
  infoContent?: string;
  infoTitle?: string;
  children?: React.ReactNode;
}
export const FieldGroup = ({
  heading,
  headerAccessories,
  infoContent,
  infoTitle,
  children
}: FieldGroupProps) => {
  return (
    <div className={styles.groupContainer}>
      <div className={styles.header}>
        <Typography>
          <span>
            {heading}
            {infoContent && (
              <>
                &nbsp;
                <YBInfoTip content={infoContent} title={infoTitle} />
              </>
            )}
          </span>
        </Typography>
        <div className={styles.accessories}>{headerAccessories}</div>
      </div>
      <div className={styles.childrenContainer}>{children}</div>
    </div>
  );
};

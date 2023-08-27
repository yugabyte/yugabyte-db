/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ReactNode } from 'react';
import clsx from 'clsx';

import styles from './FormField.module.scss';

interface FormFieldProps {
  providerNameField?: boolean;
  children?: ReactNode;
  className?: string;
}

/**
 * Styled `div` to contain a single field of a form.
 */
export const FormField = ({ className, children, providerNameField = false }: FormFieldProps) => {
  return (
    <div
      className={clsx(styles.formField, providerNameField && styles.providerNameField, className)}
    >
      {children}
    </div>
  );
};

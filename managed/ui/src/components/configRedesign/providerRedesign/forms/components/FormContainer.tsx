/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { FormEventHandler, ReactNode } from 'react';
import clsx from 'clsx';

import styles from './FormContainer.module.scss';

interface FormContainerProps {
  name: string;
  onSubmit: FormEventHandler<HTMLFormElement>;

  className?: string;
  children?: ReactNode;
}

export const FormContainer = ({ name, onSubmit, className, children }: FormContainerProps) => {
  return (
    <form className={clsx(styles.formContainer, className)} name={name} onSubmit={onSubmit}>
      {children}
    </form>
  );
};

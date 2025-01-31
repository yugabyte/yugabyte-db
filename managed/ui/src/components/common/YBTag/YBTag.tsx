/*
 * Created on Tue May 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';

import './YBTag.scss';

export enum YBTag_Types {
  PRIMARY = 'primary',
  YB_ORANGE = 'yb-orange',
  PLAIN_TEXT = 'plain-text',
  YB_GRAY = 'yb-gray'
}

interface YBTagProps {
  type?: YBTag_Types;
  className?: string | CSSStyleDeclaration;
}

export const YBTag: FC<YBTagProps> = ({ type = YBTag_Types.YB_ORANGE, children, className }) => {
  if (!children) return null;
  return <span className={clsx('yb-tag', type, className)}>{children}</span>;
};

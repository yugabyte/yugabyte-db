// Copyright (c) YugaByte, Inc.
import { Link } from 'react-router';

export const YBBreadcrumb = (props: any) => {
  return (
    <span>
      <Link {...props}>{props.children}</Link>
      <i className="fa fa-angle-right fa-fw"></i>
    </span>
  );
};

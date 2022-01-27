import React from 'react';
import clsx from 'clsx';

import './stylesheets/YBBanner.scss';

export const YBBanner = (props) => {
  const { children, variant, className, ...rest } = props;

  //Add more variants as needed
  const bannerClass = clsx(
    'yb-banner',
    className && className,
    variant === 'WARNING' && 'yb-banner-warning'
  );

  return (
    <div className={bannerClass} {...rest}>
      {children}
    </div>
  );
};

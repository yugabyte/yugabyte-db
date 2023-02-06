import React, { FC } from 'react';
import type { InputBaseComponentProps } from '@material-ui/core';

export const YBStripeInput: FC<InputBaseComponentProps> = React.forwardRef(
  (props: InputBaseComponentProps, ref: React.Ref<unknown>) => {
    const { component: Component, handleOnChange, handleOnBlur, ...rest } = props;

    return (
      <Component
        {...rest}
        ref={ref}
        onChange={handleOnChange as unknown}
        onBlur={handleOnBlur as unknown}
      />
    );
  }
);

YBStripeInput.displayName = 'YBStripeInput';

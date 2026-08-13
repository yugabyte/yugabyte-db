import { FC, ReactNode } from 'react';
import { mui } from '@yugabyte-ui-library/core';

const { Grow, Popper } = mui;

const OPEN_TRANSITION_MS = 280;

interface OnboardingTourPopperProps {
  open: boolean;
  anchorEl: HTMLElement | null;
  placement: Parameters<typeof Popper>[0]['placement'];
  offset: [number, number];
  zIndex?: number | ((theme: { zIndex: { modal: number } }) => number | string);
  children: ReactNode;
}

const getTransformOrigin = (placement: string): string => {
  if (placement.startsWith('bottom')) return 'top center';
  if (placement.startsWith('top')) return 'bottom center';
  if (placement.startsWith('left')) return 'right center';
  return 'left center';
};

/**
 * Shared Popper for onboarding tips with a short fade + scale enter/exit.
 */
export const OnboardingTourPopper: FC<OnboardingTourPopperProps> = ({
  open,
  anchorEl,
  placement,
  offset,
  zIndex = 2200,
  children
}) => (
  <Popper
    open={open}
    anchorEl={anchorEl}
    placement={placement}
    transition
    modifiers={[
      {
        name: 'offset',
        options: {
          offset
        }
      }
    ]}
    sx={{ zIndex }}
  >
    {({ TransitionProps, placement: popperPlacement }) => (
      <Grow
        {...TransitionProps}
        timeout={OPEN_TRANSITION_MS}
        style={{ transformOrigin: getTransformOrigin(popperPlacement) }}
      >
        <div>{children}</div>
      </Grow>
    )}
  </Popper>
);

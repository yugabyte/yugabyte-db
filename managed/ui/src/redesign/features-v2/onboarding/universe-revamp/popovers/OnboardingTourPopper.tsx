import { FC, ReactNode } from 'react';
import { mui } from '@yugabyte-ui-library/core';

const { ClickAwayListener, Grow, Popper } = mui;

const OPEN_TRANSITION_MS = 280;

interface OnboardingTourPopperProps {
  open: boolean;
  anchorEl: HTMLElement | null;
  placement: Parameters<typeof Popper>[0]['placement'];
  offset: [number, number];
  zIndex?: number | ((theme: { zIndex: { modal: number } }) => number | string);
  /**
   * When set, the popper portals into this element and is kept inside its bounds
   * via preventOverflow / flip (used by Advanced Placement inside Edit Universe).
   */
  boundaryEl?: HTMLElement | null;
  /** Close / click-away — both persist dismissal. */
  onClickAway?: () => void;
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
 * Click-away closes without blocking interaction with the rest of the page.
 */
export const OnboardingTourPopper: FC<OnboardingTourPopperProps> = ({
  open,
  anchorEl,
  placement,
  offset,
  zIndex = 2200,
  boundaryEl,
  onClickAway,
  children
}) => {
  const handleClickAway = (event: MouseEvent | TouchEvent) => {
    const el = event.target as Element | null;
    // Clicks inside modals (e.g. BeforeProceed) must not dismiss the tip.
    if (el?.closest?.('.MuiModal-root, [role="dialog"]')) return;
    onClickAway?.();
  };

  return (
    <Popper
      open={open}
      anchorEl={anchorEl}
      placement={placement}
      transition
      container={boundaryEl ?? undefined}
      modifiers={[
        {
          name: 'offset',
          options: {
            offset
          }
        },
        ...(boundaryEl
          ? [
              {
                name: 'preventOverflow' as const,
                options: {
                  boundary: boundaryEl,
                  padding: 8
                }
              },
              // Flipping would leave the card's arrow pointing away from the anchor,
              // since the tour card renders a fixed arrow side.
              { name: 'flip' as const, enabled: false }
            ]
          : [])
      ]}
      sx={{ zIndex }}
    >
      {({ TransitionProps, placement: popperPlacement }) => (
        <Grow
          {...TransitionProps}
          timeout={OPEN_TRANSITION_MS}
          style={{ transformOrigin: getTransformOrigin(popperPlacement) }}
        >
          <div>
            {onClickAway ? (
              <ClickAwayListener onClickAway={handleClickAway} mouseEvent="onMouseDown">
                <div>{children}</div>
              </ClickAwayListener>
            ) : (
              children
            )}
          </div>
        </Grow>
      )}
    </Popper>
  );
};

import { FC, MouseEvent, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { OnboardingTourPopper } from './OnboardingTourPopper';

/** Gap between the dropdown trigger and the left-placed tip. */
const POPOVER_OFFSET: [number, number] = [0, 8];

export const ADVANCED_PLACEMENT_POPOVER_DISMISS_KEY = 'yb_advanced_placement_popover_dismissed';

interface AdvancedPlacementPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  onClose: () => void;
}

export const isAdvancedPlacementPopoverDismissed = (): boolean =>
  localStorage.getItem(ADVANCED_PLACEMENT_POPOVER_DISMISS_KEY) === 'true';

export const dismissAdvancedPlacementPopover = (): void => {
  localStorage.setItem(ADVANCED_PLACEMENT_POPOVER_DISMISS_KEY, 'true');
};

export const shouldInterceptAdvancedPlacementClick = (): boolean =>
  !isAdvancedPlacementPopoverDismissed();

export const useAdvancedPlacementPopover = (autoOpen = false) => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    if (!autoOpen || isAdvancedPlacementPopoverDismissed()) {
      return;
    }
    setOpen(true);
  }, [autoOpen]);

  const handleAdvancedPlacementClick = useCallback((event: MouseEvent) => {
    if (!shouldInterceptAdvancedPlacementClick()) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    setOpen(true);
  }, []);

  const handleClose = useCallback(() => {
    dismissAdvancedPlacementPopover();
    setOpen(false);
  }, []);

  return {
    open,
    anchorRef,
    handleAdvancedPlacementClick,
    handleClose
  };
};

export const AdvancedPlacementPopover: FC<AdvancedPlacementPopoverProps> = ({
  open,
  anchorRef,
  onClose
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.advancedPlacementPopover'
  });

  // Anchor to the trigger button — the wrapper span is as wide as the dropdown menu (340px).
  const dropdownButton =
    (anchorRef.current?.querySelector(
      '[data-testid="edit-placement-actions-button"]'
    ) as HTMLElement | null) ?? anchorRef.current;

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={dropdownButton}
      placement={TourPlacement.Left}
      offset={POPOVER_OFFSET}
    >
      <YBTourSpotlight
        title={t('title')}
        body={t('body')}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.Left}
        dataTestId="advanced-placement-popover-spotlight"
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};

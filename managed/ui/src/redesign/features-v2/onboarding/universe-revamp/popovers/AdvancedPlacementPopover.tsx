import { FC, MouseEvent, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';

const { Popper } = mui;

/** Skidding shifts the left-placed popover downward along the anchor. */
const POPOVER_OFFSET: [number, number] = [28, 12];

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

  return (
    <Popper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.Left}
      modifiers={[
        {
          name: 'offset',
          options: {
            offset: POPOVER_OFFSET
          }
        }
      ]}
      sx={{ zIndex: 2200 }}
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
    </Popper>
  );
};

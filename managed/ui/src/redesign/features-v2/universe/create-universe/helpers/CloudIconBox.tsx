import type { ReactNode } from 'react';
import { mui } from '@yugabyte-ui-library/core';
import { BASE_CLOUD_OPTIONS } from './generalSettingsDefaults';

const { Box } = mui;

/** Native size of `@yugabyte-ui-library` cloud option logos (no viewBox). */
const CLOUD_ICON_NATIVE_PX = 32;

/**
 * Library cloud SVGs are 32×32 with no viewBox — setting a smaller width clips paths.
 * Render at native size and CSS-scale into the slot.
 */
export function CloudIconBox({ code, size }: { code?: string; size: number }) {
  const option = BASE_CLOUD_OPTIONS.find((o) => o.value === code);
  if (!option?.icon) return null;
  const scale = size / CLOUD_ICON_NATIVE_PX;
  return (
    <Box
      component="span"
      sx={{
        width: size,
        height: size,
        display: 'inline-block',
        flexShrink: 0,
        lineHeight: 0,
        overflow: 'hidden',
        verticalAlign: 'middle'
      }}
    >
      <Box
        sx={{
          width: CLOUD_ICON_NATIVE_PX,
          height: CLOUD_ICON_NATIVE_PX,
          transform: `scale(${scale})`,
          transformOrigin: 'top left',
          '& svg': { display: 'block', width: CLOUD_ICON_NATIVE_PX, height: CLOUD_ICON_NATIVE_PX }
        }}
      >
        {option.icon as ReactNode}
      </Box>
    </Box>
  );
}

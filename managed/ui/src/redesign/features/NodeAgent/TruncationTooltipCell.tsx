import { FC, ReactElement, useLayoutEffect, useRef, useState } from 'react';
import { Box } from '@material-ui/core';

import { YBTooltip } from '../../components';

interface TruncationTooltipCellProps {
  title?: string | null;
  children: ReactElement;
}

export const TruncationTooltipCell: FC<TruncationTooltipCellProps> = ({ title, children }) => {
  const contentRef = useRef<HTMLSpanElement>(null);
  const [isTruncated, setIsTruncated] = useState(false);

  useLayoutEffect(() => {
    const element = contentRef.current;
    if (!element) {
      return;
    }

    const updateTruncation = () => {
      setIsTruncated(
        element.scrollWidth > element.clientWidth || element.scrollHeight > element.clientHeight
      );
    };

    updateTruncation();
    const observer = new ResizeObserver(updateTruncation);
    observer.observe(element);
    return () => observer.disconnect();
  }, [title, children]);

  const content = (
    <Box
      component="span"
      ref={contentRef}
      display="block"
      minWidth={0}
      maxWidth="100%"
      overflow="hidden"
    >
      {children}
    </Box>
  );

  if (!title || !isTruncated) {
    return content;
  }

  return (
    <YBTooltip title={title} placement="top">
      {content}
    </YBTooltip>
  );
};

export const formatTextWithTruncationTooltip = (value?: string | null) => {
  if (!value) {
    return <span />;
  }

  return (
    <TruncationTooltipCell title={value}>
      <span>{value}</span>
    </TruncationTooltipCell>
  );
};

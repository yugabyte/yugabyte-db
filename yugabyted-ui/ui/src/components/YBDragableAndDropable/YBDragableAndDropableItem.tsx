import React, { FC, useRef } from 'react';
import { ItemTypes } from '@app/helpers/dnd/types';
import useDropMethod from '@app/helpers/dnd/useDropMethod';
import useDragMethod from '@app/helpers/dnd/useDragMethod';
import useHoverMethod from '@app/helpers/dnd/useHoverMethod';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { themeVariables } from '@app/theme/variables';

interface YBDragableAndDropableItemProps {
  id: string;
  type: ItemTypes;
  index: number;
  data: string[];
  className?: string;
  onChange: (change: string[]) => void;
}

const useStyles = makeStyles((theme) => ({
  hovering: {
    border: '2px dashed',
    borderColor: theme.palette.divider,
    borderRadius: themeVariables.borderRadius
  },
  notHovering: {
    border: '2px dashed',
    borderColor: 'transparent'
  },
  hoveringChildren: {
    opacity: 0
  }
}));

export const YBDragableAndDropableItem: FC<YBDragableAndDropableItemProps> = ({
  id,
  type,
  index,
  children,
  data,
  onChange,
  className
}) => {
  const itemRef = useRef<HTMLDivElement>(null);
  const { drag } = useDragMethod(id, type, index);
  const { drop, isOver, draggedItemType } = useDropMethod();
  const { drop: hoverDrop } = useHoverMethod(itemRef, index, data, onChange);
  const classes = useStyles();
  drag(drop(hoverDrop(itemRef)));
  return (
    <div
      className={clsx(isOver && draggedItemType === ItemTypes.card ? classes.hovering : classes.notHovering, className)}
      ref={itemRef}
    >
      <Box className={clsx(isOver && draggedItemType === ItemTypes.card ? classes.hoveringChildren : '', className)}>
        {children}
      </Box>
    </div>
  );
};

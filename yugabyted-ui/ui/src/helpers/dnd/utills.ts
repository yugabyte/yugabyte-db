import type { DropTargetMonitor } from 'react-dnd';
import { DragItem, ItemTypes } from './types';
import type { RefObject } from 'react';

const basicCheck = (index: number, type: ItemTypes, hoverIndex: number, id?: string | undefined): boolean => {
  if (index === undefined) return false;
  if (type === ItemTypes.card && id === undefined) return false;
  return index !== hoverIndex;
};

const calculateMiddle = (hoverBoundingRect: DOMRect, monitor: DropTargetMonitor) => {
  const hoverMiddleY = (hoverBoundingRect.bottom - hoverBoundingRect.top) / 2;

  const clientOffset = monitor.getClientOffset();

  const hoverClientY = clientOffset!.y - hoverBoundingRect.top;

  return { hoverClientY, hoverMiddleY };
};

const checkUpAndDown = (index: number, hoverIndex: number, hoverMiddleY: number, hoverClientY: number): boolean => {
  return !(index > hoverIndex && hoverClientY > hoverMiddleY);
};

export const canMove = (
  ref: RefObject<HTMLDivElement>,
  item: DragItem,
  hoverIndex: number,
  monitor: DropTargetMonitor
): boolean => {
  const { index, type, id } = item;

  if (!ref.current) return false;

  const hoverBoundingRect = ref.current.getBoundingClientRect();

  const { hoverMiddleY, hoverClientY } = calculateMiddle(hoverBoundingRect, monitor);

  return basicCheck(index, type, hoverIndex, id) && checkUpAndDown(index, hoverIndex, hoverMiddleY, hoverClientY);
};

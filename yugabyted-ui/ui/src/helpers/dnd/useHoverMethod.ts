import type { RefObject } from 'react';
import { ConnectDropTarget, DropTargetMonitor, useDrop } from 'react-dnd';
import { DragItem, ItemTypes } from './types';
import { canMove } from './utills';

const useHoverMethod = (
  ref: RefObject<HTMLDivElement>,
  hoverIndex: number,
  data: string[],
  onChange: (change: string[]) => void
): { drop: ConnectDropTarget } => {
  const [, drop] = useDrop({
    accept: [ItemTypes.card],
    hover: (item: DragItem, monitor: DropTargetMonitor) => {
      if (canMove(ref, item, hoverIndex, monitor)) {
        const dragged = data[item.index];
        data.splice(item.index, 1);
        data.splice(hoverIndex, 0, dragged);
        onChange(data);
        item.index = hoverIndex;
      }
      return null;
    }
  });
  return { drop };
};

export default useHoverMethod;

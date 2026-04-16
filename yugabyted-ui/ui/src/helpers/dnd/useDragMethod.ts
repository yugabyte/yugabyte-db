import { ConnectDragSource, useDrag } from 'react-dnd';
import { ItemTypes } from './types';

const useDragMethod = (id: string, type: ItemTypes, index: number): { drag: ConnectDragSource } => {
  const [, drag] = useDrag({
    canDrag: undefined,
    options: undefined,
    previewOptions: undefined,
    type: ItemTypes.card,
    item: {
      type,
      id,
      index
    }
  });

  return { drag };
};

export default useDragMethod;

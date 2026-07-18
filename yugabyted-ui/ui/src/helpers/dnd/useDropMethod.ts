import { ConnectDropTarget, useDrop } from 'react-dnd';
import { ItemTypes } from './types';

interface ReturnedTypeUseDropMethod {
  drop: ConnectDropTarget;
  isOver: boolean;
  draggedItemType: symbol | string | null;
}

const useDropMethod = (): ReturnedTypeUseDropMethod => {
  const [{ isOver, draggedItemType }, drop] = useDrop({
    accept: [ItemTypes.card],
    collect: (monitor) => ({
      isOver: monitor.isOver({ shallow: true }),
      draggedItemType: monitor.getItemType()
    })
  });

  return { drop, isOver, draggedItemType };
};

export default useDropMethod;

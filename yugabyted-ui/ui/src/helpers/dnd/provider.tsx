import React, { FC } from 'react';
import { DndProvider } from 'react-dnd';
import { HTML5Backend } from 'react-dnd-html5-backend';

export const DnDProvider: FC = ({ children }) => {
  return <DndProvider backend={HTML5Backend}>{children}</DndProvider>;
};

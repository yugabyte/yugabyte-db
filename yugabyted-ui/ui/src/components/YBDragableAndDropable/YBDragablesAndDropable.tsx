import React, { FC, useRef } from 'react';
import useDropMethod from '@app/helpers/dnd/useDropMethod';
import { DnDProvider } from '@app/helpers/dnd/provider';

interface YBDragableAndDropableProps {
  className?: string;
}

const YBDragableAndDropableContainer: FC<YBDragableAndDropableProps> = ({ className, children }) => {
  const itemRef = useRef<HTMLDivElement>(null);
  const { drop } = useDropMethod();
  drop(itemRef);
  return (
    <div className={className} ref={itemRef}>
      {children}
    </div>
  );
};

export const YBDragableAndDropable: FC<YBDragableAndDropableProps> = ({ children, className }) => {
  return (
    <DnDProvider>
      <YBDragableAndDropableContainer className={className}>{children}</YBDragableAndDropableContainer>
    </DnDProvider>
  );
};

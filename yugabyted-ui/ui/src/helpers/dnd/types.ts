export enum Dropzones {
  left = 'left', // for future use
  right = 'right', // for future use
  main = 'main'
}

export enum ItemTypes {
  card = 'Card'
}

export interface DragItem {
  type: ItemTypes;
  id: string;
  name: string;
  index: number;
  itemId?: string;
}

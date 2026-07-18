import type { AzUpgradeStep } from '../types';

/** Droppable ID is just the slot index (unique within each DragDropContext). */
export const getSlotId = (index: number) => String(index);

export const parseSlotIndex = (droppableId: string): number | null => {
  const index = parseInt(droppableId, 10);
  return Number.isNaN(index) || index < 0 ? null : index;
};

/** Returns a new array with elements at indexA and indexB swapped. */
export const swapAt = <T,>(array: T[], indexA: number, indexB: number): T[] => {
  const result = [...array];
  const valueA = result[indexA];
  const valueB = result[indexB];
  if (valueA !== undefined && valueB !== undefined) {
    result[indexA] = valueB;
    result[indexB] = valueA;
  }
  return result;
};

/** Swap two slots in order and swap their pause-after values so pause stays by position. */
export const applySlotSwap = (
  azOrder: string[],
  azSteps: Record<string, AzUpgradeStep>,
  sourceIndex: number,
  destIndex: number
): { newAzOrder: string[]; newAzSteps: Record<string, AzUpgradeStep> } => {
  const azUuidA = azOrder[sourceIndex];
  const azUuidB = azOrder[destIndex];
  const pauseA = azSteps[azUuidA]?.pauseAfterTserverUpgrade ?? false;
  const pauseB = azSteps[azUuidB]?.pauseAfterTserverUpgrade ?? false;

  const newAzSteps = { ...azSteps };
  if (azUuidA !== undefined && azUuidB !== undefined) {
    if (newAzSteps[azUuidA]) {
      newAzSteps[azUuidA] = { ...newAzSteps[azUuidA], pauseAfterTserverUpgrade: pauseB };
    }
    if (newAzSteps[azUuidB]) {
      newAzSteps[azUuidB] = { ...newAzSteps[azUuidB], pauseAfterTserverUpgrade: pauseA };
    }
  }

  const newAzOrder = swapAt(azOrder, sourceIndex, destIndex);

  return { newAzOrder, newAzSteps };
};

import { Dispatch, ReactNode, SetStateAction, useEffect, useState } from 'react';
import clsx from 'clsx';
import './MenuItemsContainer.scss';

interface MenuItemsContainerProps {
  mainMenu: (setActiveSubmenu: Dispatch<SetStateAction<null | string>>) => ReactNode;
  parentDropdownOpen: Boolean;
  subMenus: {
    [activeSubmenu: string]: (setActiveSubmenu: (subMenu: string | null) => void) => ReactNode;
  };
}
export const MenuItemsContainer = ({
  mainMenu,
  subMenus,
  parentDropdownOpen
}: MenuItemsContainerProps) => {
  const [activeSubmenu, setActiveSubmenu] = useState<string | null>(null);

  // reset active submenu when parent dropdown closed
  useEffect(() => {
    if (!parentDropdownOpen && activeSubmenu !== null) setActiveSubmenu(null);
  }, [parentDropdownOpen, activeSubmenu]);

  return (
    <div className="menu-items-container">
      <div
        className={clsx('menu-items-container__main-frame', {
          'menu-items-container__main-frame--slide-right': activeSubmenu === null,
          'menu-items-container__main-frame--slide-left': activeSubmenu !== null
        })}
      >
        {mainMenu(setActiveSubmenu)}
      </div>
      <div className="menu-items-container__submenu-frame">
        {activeSubmenu && subMenus[activeSubmenu](setActiveSubmenu)}
      </div>
    </div>
  );
};

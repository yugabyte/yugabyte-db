setTimeout(() => {
    const asideMenu = document.getElementsByClassName('sphinxsidebarwrapper')[0];
    hideSubMenus();
    asideMenu.style.display = 'block';
}, 500);

function hideSubMenus() {
    const asideMenu = document.getElementsByClassName('sphinxsidebarwrapper')[0];
    const activeCheckboxClass = 'custom-button--active';
    const activeBackgroundClass = 'custom-button--main-active';
    const links = Array.from(asideMenu.getElementsByTagName('a'));
    const accordionLinks = links.filter(links => links.nextElementSibling && links.nextElementSibling.localName === 'ul');
    const simpleLinks = links.filter(links => !links.nextElementSibling && links.parentElement.localName === 'li');

    simpleLinks.forEach(simpleLink => {
        simpleLink.parentElement.style.listStyleType = 'disc';
        simpleLink.parentElement.style.marginLeft = '20px';
    });

    accordionLinks.forEach((link, index) => {
        insertButton(link, index);
    });

    const buttons = Array.from(document.getElementsByClassName('custom-button'));

    buttons.forEach(button => button.addEventListener('click', event => {
        event.preventDefault();
        const current = event.currentTarget;
        const parent = current.parentElement;
        const isMain = Array.from(parent.classList).includes('toctree-l1');
        const isMainActive = Array.from(parent.classList).includes(activeBackgroundClass);
        const targetClassList = Array.from(current.classList);

        toggleElement(targetClassList.includes(activeCheckboxClass), current, activeCheckboxClass);
        if (isMain) {
            toggleElement(isMainActive, parent, activeBackgroundClass);
        }
    }));

// WIP    var toctree_heading = document.getElementById("toctree-heading");
// NOT NEEDED?    asideMenu.parentNode.insertBefore(styleDomEl, asideMenu);
}

function toggleElement(condition, item, className) {
    const isButton = item.localName === 'button';

    if (!condition) {
        const previousActive = Array.from(item.parentElement.parentElement.getElementsByClassName('list-item--active'));
        if (isButton) {
            localStorage.setItem(item.id, 'true');

            if (previousActive.length) {
                previousActive.forEach(previous => {

                    const previousActiveButtons = Array.from(previous.getElementsByClassName('custom-button--active'));
                    removeClass(previous, ['list-item--active', 'custom-button--main-active']);

                    if (previousActiveButtons.length) {
                        previousActiveButtons.forEach(previousButton => {

                            removeClass(previousButton, 'custom-button--active');
                            localStorage.removeItem(previousButton.id);
                        });
                    }
                })
            }
        }
        addClass(item, className);
        addClass(item.parentElement, 'list-item--active');
    } else {
        removeClass(item, className);
        removeClass(item.parentElement, 'list-item--active');

        if (isButton) {
            localStorage.removeItem(item.id);
        }
    }
}
function addClass(item, classes) {
    item.classList.add(...Array.isArray(classes) ? classes : [classes]);
}
function removeClass(item, classes) {
    item.classList.remove(...Array.isArray(classes) ? classes : [classes]);
}
function insertButton(element, id) {
    const button = document.createElement('button');
    const isMain = Array.from(element.parentElement.classList).includes('toctree-l1');
    button.id = id;
    addClass(button, 'custom-button');
    if (localStorage.getItem(id)) {
        addClass(button, 'custom-button--active');
        addClass(element.parentElement, 'list-item--active');
        if (isMain) {
            addClass(element.parentElement, 'custom-button--main-active');
        }
    }
    element.insertAdjacentElement('beforebegin', button);
}
function makeSelect() {
    const custom_select = document.getElementById('custom_select');
    const select_active_option = custom_select.getElementsByClassName('select-active-text')[0];
    const custom_select_list = document.getElementById('custom_select_list');

    select_active_option.innerHTML = window.location.href.includes('') ?
        custom_select_list.getElementsByClassName('custom-select__option')[1].innerHTML :
        custom_select_list.getElementsByClassName('custom-select__option')[0].innerHTML;

    document.addEventListener('click', event => {
        if (event.target.parentElement.id === 'custom_select' || event.target.id === 'custom_select') {
            custom_select_list.classList.toggle('select-hidden')
        }
        if (Array.from(event.target.classList).includes('custom-select__option')) {
            select_active_option.innerHTML = event.target.innerHTML;
        }
        if (event.target.id !== 'custom_select' && event.target.parentElement.id !== 'custom_select') {
            custom_select_list.classList.add('select-hidden')
        }

    });
}
import { useCallback, useState } from "react";
import { browserHistory } from "react-router";
import { useMount } from "react-use";
import { YBModal } from "../components/common/forms/fields";
import { Table } from "react-bootstrap";

export const BindShortCutKeys = (props: any) => {

    const [showShortcutModal, setShowShortcutModal] = useState(false);

    const _keyEvent = useCallback((param) => {
        switch (param.key) {
            case 'N':
                browserHistory.push('/universes/create');
                break;
            case 'C':
                browserHistory.push('/config');
                break;
            case 'M':
                browserHistory.push('/metrics');
                break;
            case 'T':
                browserHistory.push('/tasks');
                break;
            case 'L':
                browserHistory.push('/universes');
                break;
            case 'D':
                browserHistory.push('/');
                break;
            default:
                break;
        }
    }, []);

    useMount(() => {
        props.bindShortcut(
            [
                'ctrl+shift+n',
                'ctrl+shift+m',
                'ctrl+shift+t',
                'ctrl+shift+l',
                'ctrl+shift+c',
                'ctrl+shift+d'
            ],
            _keyEvent
        );
        props.bindShortcut('?', () => { setShowShortcutModal(true); });

    });
    return (
        <YBModal
            title={'Keyboard Shortcut'}
            visible={showShortcutModal}
            onHide={() => setShowShortcutModal(false)}
        >
            <Table responsive>
                <thead>
                    <tr>
                        <th>Shortcut</th>
                        <th>Description</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td>?</td>
                        <td>Show help</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT + n</td>
                        <td>Create new universe</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT + e</td>
                        <td>Edit universe</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT + c</td>
                        <td>Provider config</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT + m</td>
                        <td>View metrics</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT + t</td>
                        <td>View tasks</td>
                    </tr>
                    <tr>
                        <td>CTRL + SHIFT +l</td>
                        <td>View universes</td>
                    </tr>
                </tbody>
            </Table>
        </YBModal>
    );
};

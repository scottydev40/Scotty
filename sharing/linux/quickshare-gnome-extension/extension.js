// Quick Share — GNOME Shell Quick Settings tile.
//
// Adds a tile (and an optional panel indicator icon) that controls the Nearby /
// Quick Share Linux app over D-Bus. The app exposes:
//
//   dev.scotty.Scotty  at  /dev/scotty/Scotty
//     GetVisibility() -> i          (0 Everyone, 1 Contacts, 2 Hidden)
//     SetVisibility(i)
//     GetRunning() -> b
//     Show()
//     Quit()
//     signal VisibilityChanged(i)
//     signal RunningChanged(b)

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {
    QuickMenuToggle,
    SystemIndicator,
} from 'resource:///org/gnome/shell/ui/quickSettings.js';

const BUS_NAME = 'dev.scotty.Scotty';
const OBJECT_PATH = '/dev/scotty/Scotty';
const APP_BINARY = 'scotty';

const IFACE = `
<node>
  <interface name="dev.scotty.Scotty">
    <method name="GetVisibility"><arg type="i" direction="out"/></method>
    <method name="SetVisibility"><arg type="i" direction="in"/></method>
    <method name="GetRunning"><arg type="b" direction="out"/></method>
    <method name="Show"/>
    <method name="Quit"/>
    <method name="SetTileActive"><arg type="b" direction="in"/></method>
    <method name="GetTileActive"><arg type="b" direction="out"/></method>
    <signal name="VisibilityChanged"><arg type="i"/></signal>
    <signal name="RunningChanged"><arg type="b"/></signal>
    <signal name="TileActiveChanged"><arg type="b"/></signal>
  </interface>
</node>`;

const QuickShareProxy = Gio.DBusProxy.makeProxyWrapper(IFACE);

// Matches the wording Android uses in its own Quick Share tile.
const VIS_LABEL = {
    0: 'Everyone',
    1: 'Contacts',
    2: 'Hidden',
};

const QuickShareToggle = GObject.registerClass(
class QuickShareToggle extends QuickMenuToggle {
    _init(ext) {
        super._init({
            title: 'Quick Share',
            gicon: ext.icon,
            toggleMode: true,
        });
        this._ext = ext;

        this.menu.setHeader(ext.icon, 'Quick Share', VIS_LABEL[0]);

        // Visibility radio items.
        this._items = {};
        for (const [val, label] of Object.entries(VIS_LABEL)) {
            const mode = Number(val);
            const item = new PopupMenu.PopupMenuItem(label);
            item.connect('activate', () => this._ext.setVisibility(mode));
            this.menu.addMenuItem(item);
            this._items[mode] = item;
        }

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        const openItem = new PopupMenu.PopupMenuItem('Open Quick Share');
        openItem.connect('activate', () => this._ext.openWindow());
        this.menu.addMenuItem(openItem);

        this._quitItem = new PopupMenu.PopupMenuItem('Quit Quick Share');
        this._quitItem.connect('activate', () => this._ext.quitApp());
        this.menu.addMenuItem(this._quitItem);

        // Toggling the tile itself: on → Everyone, off → Hidden.
        this.connect('clicked', () => {
            this._ext.setVisibility(this.checked ? 0 : 2);
        });
    }

    syncVisibility(vis) {
        this.checked = vis !== 2;
        this.subtitle = VIS_LABEL[vis] ?? '';
        this.menu.setHeader(this._ext.icon, 'Quick Share', VIS_LABEL[vis] ?? '');
        for (const [val, item] of Object.entries(this._items)) {
            item.setOrnament(Number(val) === vis
                ? PopupMenu.Ornament.DOT
                : PopupMenu.Ornament.NONE);
        }
    }

    syncRunning(running) {
        this.reactive = true;
        if (running)
            return;
        // App gone: the tile must not keep showing the last live state.
        this.checked = false;
        this.subtitle = 'Not running';
        this.menu.setHeader(this._ext.icon, 'Quick Share', 'Not running');
        for (const item of Object.values(this._items))
            item.setOrnament(PopupMenu.Ornament.NONE);
    }
});

const QuickShareIndicator = GObject.registerClass(
class QuickShareIndicator extends SystemIndicator {
    _init(ext) {
        super._init();
        this._panelIcon = this._addIndicator();
        this._panelIcon.gicon = ext.icon;
        this._panelIcon.visible = false; // shown only while advertising

        this.toggle = new QuickShareToggle(ext);
        this.quickSettingsItems.push(this.toggle);
    }

    setPanelVisible(visible) {
        this._panelIcon.visible = visible;
    }
});

export default class QuickShareExtension extends Extension {
    enable() {
        // Two-arrow loop, matching the app's own swap mark, and stock so it
        // recolors cleanly in the panel/menu. The first name is the closest
        // match but ships with Yaru rather than Adwaita, so fall back for
        // desktops that don't have it. (A custom fill-based symbolic could
        // replace the lot later — stroke-based SVGs don't recolor.)
        this.icon = new Gio.ThemedIcon({
            names: [
                'emblem-synchronizing-symbolic',
                'media-playlist-repeat-symbolic',
                'view-refresh-symbolic',
            ],
        });
        this._pendingVisibility = null;

        this._indicator = new QuickShareIndicator(this);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);

        // Async proxy. Auto-tracks the app appearing/disappearing on the bus.
        this._proxy = new QuickShareProxy(
            Gio.DBus.session, BUS_NAME, OBJECT_PATH,
            (proxy, error) => {
                if (error) {
                    logError(error, 'Quick Share: failed to create D-Bus proxy');
                    return;
                }
                this._onProxyReady();
            });
    }

    _onProxyReady() {
        this._visSignal = this._proxy.connectSignal(
            'VisibilityChanged', (p, s, [vis]) => this._syncVisibility(vis));
        this._runSignal = this._proxy.connectSignal(
            'RunningChanged', (p, s, [running]) => this._syncRunning(running));
        // g-name-owner flips as the app starts/stops.
        this._ownerId = this._proxy.connect(
            'notify::g-name-owner', () => this._refresh());
        this._refresh();
    }

    _refresh() {
        const running = !!this._proxy?.g_name_owner;
        this._syncRunning(running);
        if (!running)
            return;
        // Tell the app this tile is live so it drops its redundant tray icon.
        // Done on every refresh, not just enable(), because the app may have
        // started after us.
        this._proxy.SetTileActiveRemote(true, () => {});

        // A visibility picked while the app was down: apply it now that it is
        // up. Without this the launch silently restores the app's persisted
        // visibility and the choice that started it is discarded.
        if (this._pendingVisibility !== null) {
            const pending = this._pendingVisibility;
            this._pendingVisibility = null;
            this._proxy.SetVisibilityRemote(pending, (res, err) => {
                if (err)
                    logError(err, 'Quick Share: pending SetVisibility failed');
                // Sync explicitly: if the app already sat on `pending` it emits
                // no VisibilityChanged, and the tile would stay stale.
                this._syncVisibility(pending);
            });
            return;
        }

        // Pull current visibility.
        this._proxy.GetVisibilityRemote((res, err) => {
            if (err) {
                logError(err, 'Quick Share: GetVisibility failed');
                return;
            }
            const [vis] = res;
            this._syncVisibility(vis);
        });
    }

    _syncVisibility(vis) {
        this._indicator?.toggle.syncVisibility(vis);
        // Panel icon shows while advertising (not Hidden) and running.
        this._indicator?.setPanelVisible(!!this._proxy?.g_name_owner && vis !== 2);
    }

    _syncRunning(running) {
        this._indicator?.toggle.syncRunning(running);
        if (!running)
            this._indicator?.setPanelVisible(false);
    }

    setVisibility(mode) {
        if (this._proxy?.g_name_owner) {
            this._proxy.SetVisibilityRemote(mode, () => {});
        } else {
            // App not running: start it, then apply once it appears on the bus
            // (see _refresh). Launching is not instant, so say so.
            this._pendingVisibility = mode;
            // No window: setting visibility from the tile shouldn't throw one
            // in your face.
            this._launchApp(true);
            if (this._indicator)
                this._indicator.toggle.subtitle = 'Starting…';
        }
    }

    openWindow() {
        if (this._proxy?.g_name_owner)
            this._proxy.ShowRemote(() => {});
        else
            this._launchApp();
    }

    quitApp() {
        if (this._proxy?.g_name_owner)
            this._proxy.QuitRemote(() => {});
    }

    _launchApp(background = false) {
        const argv = background ? [APP_BINARY, '--background'] : [APP_BINARY];
        try {
            Gio.Subprocess.new(argv, Gio.SubprocessFlags.NONE);
        } catch (e) {
            logError(e, 'Quick Share: failed to launch app');
        }
    }

    disable() {
        // Hand control back before we go: the app restores its tray icon, so
        // the user is never left without a way to open or quit it. Sync so it
        // lands before the proxy is dropped.
        try {
            if (this._proxy?.g_name_owner)
                this._proxy.SetTileActiveSync(false);
        } catch (e) {
            logError(e, 'Quick Share: SetTileActive(false) failed');
        }

        if (this._proxy && this._visSignal)
            this._proxy.disconnectSignal(this._visSignal);
        if (this._proxy && this._runSignal)
            this._proxy.disconnectSignal(this._runSignal);
        if (this._proxy && this._ownerId)
            this._proxy.disconnect(this._ownerId);
        this._visSignal = this._runSignal = this._ownerId = null;
        this._proxy = null;
        this._pendingVisibility = null;

        this._indicator?.quickSettingsItems.forEach(i => i.destroy());
        this._indicator?.destroy();
        this._indicator = null;
        this.icon = null;
    }
}

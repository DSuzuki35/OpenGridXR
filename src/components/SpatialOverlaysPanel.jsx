import PlacementMap from "./PlacementMap.jsx";
import React, { useEffect, useId, useState } from "react";
import {
  ArrowDown,
  ArrowUp,
  Crosshair,
  Globe,
  Layers,
  Monitor,
  Move,
  Plus,
  RefreshCw,
  Settings2,
  Trash2,
} from "lucide-react";
import { api, isBrowserPreview } from "../api.mjs";
import {
  spatialDefaults,
  centeringDefaults,
  createSpatialOverlay,
  createQuickMenuOverlay,
  spatialShortcutEntries,
  effectiveOverlayRates,
} from "../spatial-overlays.mjs";
import { framegenDefaults, overlayDefaults } from "../enhancements.mjs";
import { Button, Note, Panel, Range, Segmented, Toggle } from "./Controls.jsx";
import { BindingButton, describeInput, useInputBindings } from "./BindingContext.jsx";

const STATUS_NAMES = {
  capturing: "Capturing",
  starting: "Starting",
  disabled: "Disabled",
  "budget-paused": "Paused by budget",
  "layer-limit": "Runtime layer limit",
  waiting: "Waiting for VR",
  error: "Source error",
  unavailable: "Unavailable",
  "select-window": "Select window again",
  "select-display": "Select monitor again",
  "missing-url": "URL required",
};
const PRESETS = {
  dashboard: {
    x: 0,
    y: -0.35,
    z: -1,
    yaw: 0,
    pitch: 15,
    roll: 0,
    widthMeters: 0.6,
    anchor: "local",
  },
  left: {
    x: -0.65,
    y: 0,
    z: -1,
    yaw: 25,
    pitch: 0,
    roll: 0,
    widthMeters: 0.55,
    anchor: "local",
  },
  right: {
    x: 0.65,
    y: 0,
    z: -1,
    yaw: -25,
    pitch: 0,
    roll: 0,
    widthMeters: 0.55,
    anchor: "local",
  },
  overhead: {
    x: 0,
    y: 0.55,
    z: -1,
    yaw: 0,
    pitch: -25,
    roll: 0,
    widthMeters: 0.6,
    anchor: "local",
  },
};
const MODIFIERS = [
  "",
  "Ctrl+",
  "Alt+",
  "Shift+",
  "Ctrl+Alt+",
  "Ctrl+Shift+",
  "Alt+Shift+",
  "Ctrl+Alt+Shift+",
];
function validUrl(value) {
  if (!value) return true;
  try {
    const parsed = new URL(value);
    return (
      ["http:", "https:"].includes(parsed.protocol) &&
      !parsed.username &&
      !parsed.password &&
      !/[\x00-\x20]/.test(value)
    );
  } catch {
    return false;
  }
}

function SourceSummary({ source, kind }) {
  const type = kind === "desktop" ? "Monitor" : kind === "window" ? "Window" : "Web dashboard";
  return <div className="selection-hint" role="status" aria-label="Selected source" style={{ overflowWrap: "anywhere" }}>
    <strong>{type}: {source?.name || (kind === "url" ? "No dashboard URL set" : "Select a source for this app run")}</strong>
    {source?.detail && <div>{source.detail}</div>}
    {source && <div>{source.displaying ? "Showing in VR" : "Selected · not showing in VR"}</div>}
  </div>;
}

function NumberField({ label, value, min, max, onChange }) {
  const id = useId();
  const [draft, setDraft] = useState(String(value));
  useEffect(() => setDraft(String(value)), [value]);
  return (
    <div className="text-field">
      <label htmlFor={id}>{label}</label>
      <input
        id={id}
        type="number"
        min={min}
        max={max}
        step="1"
        value={draft}
        onChange={(event) => setDraft(event.target.value)}
        onBlur={() => {
          const number = Number(draft);
          if (draft.trim() && Number.isFinite(number)) {
            const next = Math.round(Math.max(min, Math.min(max, number)));
            setDraft(String(next));
            onChange(next);
          } else setDraft(String(value));
        }}
        onKeyDown={(event) => {
          if (event.key === "Enter") event.currentTarget.blur();
          if (event.key === "Escape") setDraft(String(value));
        }}
      />
    </div>
  );
}

function ShortcutFields({ label, field, selected, profile, edit }) {
  const chord = selected[field] || "", key = chord.match(/F\d+$/)?.[0] || "";
  const modifiers = key ? chord.slice(0, -key.length) : "Ctrl+Alt+";
  const reserved = spatialShortcutEntries(profile).filter(([id, name]) => id !== selected.id || name !== field).map(([, , value]) => value).filter(Boolean);
  const domId = `menu-${field}`;
  return <div className="spatial-shortcut-grid">
    <div className="select-field"><label htmlFor={domId}>{label} key</label>
      <select id={domId} value={key} onChange={event => edit({[field]: event.target.value ? modifiers + event.target.value : ""})}>
        <option value="">Unbound</option>
        {Array.from({length:12}, (_, i) => `F${i+1}`).map(value => <option key={value} value={value} disabled={reserved.includes(modifiers + value)}>{value}</option>)}
      </select>
    </div>
    <div className="select-field"><label htmlFor={domId + "-modifiers"}>{label} modifiers</label>
      <select id={domId + "-modifiers"} value={modifiers} disabled={!key} onChange={event => edit({[field]:event.target.value + key})}>
        {MODIFIERS.map(value => <option key={value} value={value} disabled={!!key && reserved.includes(value + key)}>{value ? value.slice(0,-1).replaceAll("+", " + ") : "None"}</option>)}
      </select>
    </div>
  </div>;
}

function CenteringControls({
  profile,
  centering,
  onChange,
  run,
  dirty,
  busy,
  gameName,
}) {
  const bindings = useInputBindings();
  const binding = bindings?.settings.bindings.find(item => item.actionId === "centering.game");
  const key = centering.gameShortcut.match(/F\d+$/)?.[0] || "";
  const modifiers = key ? centering.gameShortcut.slice(0, -key.length) : "";
  const reserved = [
    (profile.framegen || framegenDefaults()).toggleShortcut,
    (profile.overlay || overlayDefaults()).toggleShortcut,
    (profile.overlay || overlayDefaults()).cycleShortcut,
    ...(profile.spatialOverlays?.items || []).filter(item => item.kind === "menu").flatMap(item => [item.shortcut, item.selectShortcut]),
  ].filter(Boolean);
  const collision =
    !!centering.gameShortcut && reserved.includes(centering.gameShortcut);
  return (
    <Panel title="Center VR in game" icon={Crosshair}>
      <p className="panel-description">
        Assign the same centering shortcut in {gameName}. This sends that game
        action; overlay-anchor centering is separate.
      </p>
      <div className="spatial-shortcut-grid">
        <div className="select-field">
          <label htmlFor="game-center-key">Game centering key</label>
          <select
            id="game-center-key"
            value={key}
            onChange={(event) =>
              onChange({
                gameShortcut: event.target.value
                  ? modifiers + event.target.value
                  : "",
              })
            }
          >
            <option value="">Unbound</option>
            {Array.from({ length: 12 }, (_, i) => `F${i + 1}`).map((value) => (
              <option
                key={value}
                value={value}
                disabled={reserved.includes(modifiers + value)}
              >
                {value}
              </option>
            ))}
          </select>
        </div>
        <div className="select-field">
          <label htmlFor="game-center-modifiers">Key modifiers</label>
          <select
            id="game-center-modifiers"
            value={modifiers}
            disabled={!key}
            onChange={(event) =>
              onChange({ gameShortcut: event.target.value + key })
            }
          >
            {MODIFIERS.map((value) => (
              <option
                key={value}
                value={value}
                disabled={!!key && reserved.includes(value + key)}
              >
                {value ? value.slice(0, -1).replaceAll("+", " + ") : "None"}
              </option>
            ))}
          </select>
        </div>
      </div>
      {bindings && (
        <div className="binding-action-row">
          <div>
            <strong>Wheel / joystick centering</strong>
            <span>
              {binding ? `${binding.deviceName} - ${describeInput(binding)}` : "Bind a button or POV direction"}
            </span>
          </div>
          <BindingButton actionId="centering.game" disabled={collision || !key} />
          {binding && (
            <button
              type="button"
              className="icon-button"
              aria-label="Remove binding: Center VR in game"
              disabled={!!busy}
              onClick={() => bindings.remove("centering.game")}
            >
              <Trash2 size={14} />
            </button>
          )}
        </div>
      )}
      <Range
        label="Centering delay"
        min={1}
        max={5}
        unit="s"
        value={centering.delaySeconds}
        onChange={(delaySeconds) =>
          onChange({ delaySeconds: Math.round(delaySeconds) })
        }
      />
      <Note>
        After clicking or pressing the bound controller input, bring {gameName}
        {" "}to the foreground within {centering.delaySeconds} seconds. Save the
        profile to activate the binding. Controller actions pause while there
        are unsaved edits. The shortcut is sent only when the selected sim is
        foreground.
      </Note>
      {collision && (
        <Note tone="amber">
          This shortcut is already assigned to frame generation or the
          performance HUD. Choose another combination.
        </Note>
      )}
      <Button
        variant="primary"
        icon={Crosshair}
        disabled={
          !!busy ||
          dirty ||
          !centering.gameShortcut ||
          collision ||
          isBrowserPreview
        }
        onClick={() =>
          run(
            "center-game",
            () => api.centerGame(profile.game),
            (result) => result.message,
          )
        }
      >
        {dirty ? "Save profile before centering" : "Center VR in game"}
      </Button>
    </Panel>
  );
}

export default function SpatialOverlaysPanel({
  profile,
  gameName,
  onChange,
  onCenterChange,
  run,
  busy,
  dirty,
}) {
  const settings = { ...spatialDefaults(), ...profile.spatialOverlays };
  const centering = { ...centeringDefaults(), ...profile.centering };
  const [selectedId, setSelectedId] = useState(settings.items[0]?.id || "");
  const [snapshot, setSnapshot] = useState(null);
  const [statusError, setStatusError] = useState("");
  const [windows, setWindows] = useState([]);
  const [windowChoice, setWindowChoice] = useState("");
  const [displays, setDisplays] = useState([]);
  const [displayChoice, setDisplayChoice] = useState("");
  const [chosenSources, setChosenSources] = useState({});
  const selected =
    settings.items.find((item) => item.id === selectedId) || settings.items[0];
  const rates = effectiveOverlayRates(settings);
  const configuredMP = settings.items.reduce(
    (sum, item, index) => sum + (item.width * item.height * rates[index]) / 1e6,
    0,
  );
  const status = snapshot?.game === profile.game ? snapshot.value : null;
  const selectedStatus = status?.items?.find(
    (item) => item.id === selected?.id,
  );
  const selectedIndex = selected
    ? settings.items.findIndex((item) => item.id === selected.id)
    : -1;
  const urlInvalid = selected?.kind === "url" && !validUrl(selected.url);
  const desktopKey = selected?.shortcut?.match(/F\d+$/)?.[0] || "";
  const desktopModifiers = desktopKey ? selected.shortcut.slice(0, -desktopKey.length) : "";
  const precisionKey = selected?.precisionShortcut?.match(/F\d+$/)?.[0] || "";
  const precisionModifiers = precisionKey ? selected.precisionShortcut.slice(0, -precisionKey.length) : "";
  const menuShortcuts = settings.items.filter(item => item.kind === "menu").flatMap(item => [item.shortcut, item.selectShortcut]).filter(Boolean);
  const sourceReference = selected?.kind === "desktop" ? selected.displayId : selected?.kind === "window" ? selected.windowId : selected?.url;
  const chosenSource = chosenSources[`${profile.id}:${selected?.id}`];
  const matchingSource = source => !!source && !!selected && source.kind === selected.kind && source.reference === sourceReference;
  const statusMatchesSource = !!selectedStatus && !!selected && selectedStatus.sourceKind === selected.kind && selectedStatus.sourceReference === sourceReference;
  const currentSource = matchingSource(chosenSource) && chosenSource.chosenAt > (snapshot?.at || 0) ? chosenSource :
    statusMatchesSource ? selectedStatus.source : matchingSource(selectedStatus?.source) ? selectedStatus.source : matchingSource(chosenSource) ? chosenSource :
    selected?.kind === "url" && selected.url && validUrl(selected.url) ? { name: new URL(selected.url).hostname, detail: selected.url, displaying: false } : null;
  const shortcutError = status?.shortcutErrors?.find(item => item.id === selected?.id)?.message;
  useEffect(() => {
    setSelectedId(settings.items[0]?.id || "");
    setWindowChoice("");
  }, [profile.id]);
  useEffect(() => { setWindowChoice(""); setDisplayChoice(""); }, [profile.id, selected?.id, selected?.kind]);
  useEffect(() => {
    let disposed = false,
      timer,
      inFlight = false;
    const game = profile.game;
    setSnapshot(null);
    setStatusError("");
    const poll = async () => {
      if (disposed || window.document.hidden || inFlight) return;
      inFlight = true;
      try {
        const result = await api.overlayStatus(game);
        if (!disposed && !window.document.hidden) {
          setSnapshot({ game, value: result, at: Date.now() });
          setStatusError("");
        }
      } catch (error) {
        if (!disposed) {
          setSnapshot(null);
          setStatusError(error.message);
        }
      } finally {
        inFlight = false;
        if (!disposed && !window.document.hidden)
          timer = setTimeout(poll, 1000);
      }
    };
    const visibility = () => {
      clearTimeout(timer);
      setSnapshot(null);
      if (!window.document.hidden && !inFlight) timer = setTimeout(poll, 0);
    };
    window.document.addEventListener("visibilitychange", visibility);
    timer = setTimeout(poll, 0);
    return () => {
      disposed = true;
      clearTimeout(timer);
      window.document.removeEventListener("visibilitychange", visibility);
    };
  }, [profile.game]);
  const updateItem = (id, patch) =>
    onChange({
      items: settings.items.map((item) =>
        item.id === id ? { ...item, ...patch } : item,
      ),
    });
  const edit = (patch) => {
    if (selected) updateItem(selected.id, patch);
  };
  const chooseKind = (kind) => {
    if (kind === selected.kind) return;
    if (kind === "menu") {
      if (settings.items.some(item => item.kind === "menu" && item.id !== selected.id)) return;
      const defaults = createQuickMenuOverlay();
      const reserved = spatialShortcutEntries(profile).filter(([id]) => id !== selected.id).map(([, , value]) => value);
      if (reserved.includes(defaults.shortcut)) defaults.shortcut = "";
      if (reserved.includes(defaults.selectShortcut)) defaults.selectShortcut = "";
      edit({ ...defaults, id: selected.id, name: /^Overlay \d+$/.test(selected.name) || selected.name === "New overlay" ? defaults.name : selected.name });
    } else edit(selected.kind === "menu" ? { kind, shortcut: "", selectShortcut: "", interaction: "mouse" } : { kind });
  };
  const add = () => {
    if (settings.items.length >= 15) return;
    const next = createSpatialOverlay();
    let index = 1;
    while (settings.items.some((item) => item.name === `Overlay ${index}`))
      index++;
    next.name = `Overlay ${index}`;
    onChange({ items: [...settings.items, next] });
    setSelectedId(next.id);
  };
  const remove = () => {
    if (!selected) return;
    const next = settings.items.filter((item) => item.id !== selected.id);
    onChange({ items: next });
    setSelectedId(next[Math.min(selectedIndex, next.length - 1)]?.id || "");
  };
  const move = (direction) => {
    const next = [...settings.items],
      to = selectedIndex + direction;
    if (to < 0 || to >= next.length) return;
    [next[selectedIndex], next[to]] = [next[to], next[selectedIndex]];
    onChange({ items: next });
  };
  const listWindows = () =>
    run("overlay-windows", async () => {
      setWindows(await api.listOverlayWindows());
      setWindowChoice("");
    });
  const selectWindow = () =>
    run(
      "overlay-select",
      async () => {
        const result = await api.selectOverlayWindow(
          profile.game,
          selected.id,
          windowChoice,
        );
        updateItem(selected.id, { windowId: result.id });
        setChosenSources(values => ({ ...values, [`${profile.id}:${selected.id}`]: { kind: "window", reference: result.id, name: result.name, detail: "", displaying: false, chosenAt: Date.now() } }));
        setWindowChoice("");
        return result;
      },
      (result) =>
        `Selected “${result.name}” for this app run. Save the profile to apply this source.`,
    );
  const refresh = () =>
    run("overlay-refresh", async () => {
      const value = await api.overlayStatus(profile.game);
      setSnapshot({ game: profile.game, value, at: Date.now() });
      setStatusError("");
    });
  const listDisplays = () => run("overlay-displays", async () => {
    setDisplays(await api.listOverlayDisplays()); setDisplayChoice("");
  });
  const selectDisplay = () => run("overlay-display-select", async () => {
    const result = await api.selectOverlayDisplay(profile.game, selected.id, displayChoice);
    const scale = Math.min(1, 1920 / result.width, 1080 / result.height);
    const width = Math.max(256, Math.round(result.width * scale)), height = Math.max(128, Math.round(result.height * scale));
    updateItem(selected.id, { displayId: result.id, width, height });
    setChosenSources(values => ({ ...values, [`${profile.id}:${selected.id}`]: { kind: "desktop", reference: result.id, name: result.name, detail: `${result.width} × ${result.height}`, displaying: false, chosenAt: Date.now() } }));
    setDisplayChoice(""); return result;
  }, result => `Selected “${result.name}” for this app run. Save the profile to apply this monitor.`);

  return (
    <div className="enhancement-layout spatial-layout">
      <div className="enhancement-main">
        <Panel
          title="Your overlay set"
          icon={Layers}
          action={
            <span className="badge subtle">
              {settings.items.length} / 15 SOURCES
            </span>
          }
        >
          <div className="spatial-global-enable">
            <Toggle
              label="Enable spatial overlays"
              bindingAction="spatialOverlays.enabled"
              checked={settings.enabled}
              onChange={(enabled) => onChange({ enabled })}
              description="Saved sources start only with an active, supported XR session"
            />
            <Button
              icon={Plus}
              variant="primary"
              onClick={add}
              disabled={settings.items.length >= 15}
            >
              Add overlay
            </Button>
          </div>
          {settings.items.length ? (
            <div
              className="spatial-source-list"
              role="list"
              aria-label="Configured overlays"
            >
              {settings.items.map((item, index) => {
                const actual = status?.items?.find(
                  (entry) => entry.id === item.id,
                );
                return (
                  <div key={item.id} role="listitem">
                    <button
                      className={`spatial-source-row ${selected?.id === item.id ? "selected" : ""}`}
                      onClick={() => setSelectedId(item.id)}
                      aria-label={`Select overlay ${item.name}`}
                      aria-pressed={selected?.id === item.id}
                    >
                      <span className="spatial-source-number">
                        {String(index + 1).padStart(2, "0")}
                      </span>
                      {item.kind === "menu" ? <Layers size={16} /> : item.kind === "url" ? (
                        <Globe size={16} />
                      ) : (
                        <Monitor size={16} />
                      )}
                      <span className="spatial-source-identity">
                        <span className="spatial-source-title">
                          <strong>{item.name || "Unnamed overlay"}</strong>
                          <span
                            className={`spatial-visibility-badge ${item.showInStream === true ? "shared" : ""}`}
                          >
                            {item.showInStream === true ? "Stream" : "VR only"}
                          </span>
                        </span>
                        <small>
                          {item.kind === "url"
                            ? item.url || "Web URL not set"
                            : item.kind === "menu" ? "Local headset controls"
                            : item.kind === "desktop"
                              ? item.displayId ? "Monitor reference saved" : "No monitor selected"
                              : item.windowId
                              ? "Window reference saved"
                              : "No window selected"}
                        </small>
                      </span>
                      <span className="spatial-row-state">
                        <span
                          className={`live-dot ${actual?.status === "capturing" ? "green" : !actual && settings.enabled && item.enabled ? "violet" : ""}`}
                        />
                        {actual
                          ? STATUS_NAMES[actual.status] || actual.status
                          : settings.enabled && item.enabled
                            ? "Not applied"
                            : "Disabled"}
                        <small>{rates[index]} Hz configured</small>
                      </span>
                    </button>
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="spatial-empty">
              <Layers size={31} />
              <h3>Make room for what matters.</h3>
              <p>
                Add a web dashboard or a window, then place it around your
                driving view.
              </p>
              <Button icon={Plus} onClick={add}>
                Add your first overlay
              </Button>
            </div>
          )}
          <p className="selection-hint">
            Disabled items request no source frames after saving. List order is
            priority when the budget cannot fund every source.
          </p>
        </Panel>
        {selected && (
          <>
            <Panel
              title="Selected source"
              icon={selected.kind === "menu" ? Layers : selected.kind === "url" ? Globe : Monitor}
              action={
                <div className="spatial-item-actions">
                  <button
                    className="icon-button"
                    aria-label="Move overlay up"
                    title="Move up in priority"
                    disabled={selectedIndex === 0}
                    onClick={() => move(-1)}
                  >
                    <ArrowUp size={14} />
                  </button>
                  <button
                    className="icon-button"
                    aria-label="Move overlay down"
                    title="Move down in priority"
                    disabled={selectedIndex === settings.items.length - 1}
                    onClick={() => move(1)}
                  >
                    <ArrowDown size={14} />
                  </button>
                  <button
                    className="icon-button"
                    aria-label="Delete selected overlay"
                    title="Delete overlay"
                    onClick={remove}
                  >
                    <Trash2 size={14} />
                  </button>
                </div>
              }
            >
              <div className="spatial-name-enable">
                <div className="text-field">
                  <label htmlFor="spatial-name">Overlay name</label>
                  <input
                    id="spatial-name"
                    value={selected.name}
                    maxLength={80}
                    onChange={(event) => edit({ name: event.target.value })}
                  />
                </div>
                <Toggle
                  label="Enable overlay"
                  bindingAction={`spatialOverlays.${selected.id}.enabled`}
                  checked={selected.enabled}
                  onChange={(enabled) => edit({ enabled })}
                />
              </div>
              {selected.kind !== "menu" && <div className="spatial-stream-visibility">
                <Toggle
                  label="Show in stream"
                  bindingAction={`spatialOverlays.${selected.id}.showInStream`}
                  checked={selected.showInStream === true}
                  onChange={(showInStream) => edit({ showInStream })}
                  description="Show this VR overlay in Clean Output at its VR position."
                />
                <p className="selection-hint">
                  Requires the overlay to be enabled. Extra GPU copies only
                  while Clean Output runs. Off keeps this overlay VR-only.
                </p>
              </div>}
              <Segmented
                label="Overlay source type"
                value={selected.kind}
                onChange={chooseKind}
                options={[
                  { value: "url", label: "Web URL", icon: Globe },
                  { value: "window", label: "Window", icon: Monitor },
                  { value: "desktop", label: "Monitor", icon: Monitor },
                  ...(!settings.items.some(item => item.kind === "menu" && item.id !== selected.id) ? [{ value: "menu", label: "OpenGrid menu", icon: Layers }] : []),
                ]}
              />
              {selected.kind === "menu" ? (
                <div className="spatial-menu-settings">
                  <Note>Local headset controls. Point with your head and press Select. The menu stays in your play space, recenters when shown and is always VR-only. Hidden menus stop rendering and pointing.</Note>
                  <ShortcutFields label="Menu visibility" field="shortcut" selected={selected} profile={profile} edit={edit} />
                  <ShortcutFields label="Menu select" field="selectShortcut" selected={selected} profile={profile} edit={edit} />
                  <div className="spatial-window-actions"><span>Wheel / joystick visibility</span><BindingButton actionId={`spatialOverlays.${selected.id}.enabled`} /></div>
                  <div className="spatial-window-actions"><span>Select menu item</span><BindingButton actionId={`spatialOverlays.${selected.id}.leftClick`} /></div>
                  {shortcutError && <Note tone="amber">{shortcutError}</Note>}
                  <Note>Save the profile and enable Spatial Overlays before starting VR. Visibility and select keys can be unbound. Selection uses the headset pointer and leaves your Windows cursor unchanged.</Note>
                </div>
              ) : selected.kind === "url" ? (
                <div className="text-field spatial-url-field">
                  <label htmlFor="spatial-url">Source URL</label>
                  <input
                    id="spatial-url"
                    type="url"
                    value={selected.url}
                    maxLength={2048}
                    placeholder="https://example.com/dashboard"
                    onChange={(event) => edit({ url: event.target.value })}
                    aria-invalid={urlInvalid}
                  />
                  {urlInvalid && (
                    <p className="spatial-field-error">
                      Use a complete HTTP or HTTPS URL without embedded
                      credentials.
                    </p>
                  )}
                  <p className="selection-hint">
                    The source loads after saving and starting a supported XR
                    session. This editor does not display the remote page.
                  </p>
                </div>
              ) : selected.kind === "desktop" ? (
                <div className="spatial-window-picker">
                  <div className="select-field">
                    <label htmlFor="spatial-display">Current monitors</label>
                    <select id="spatial-display" value={displayChoice} onChange={event => setDisplayChoice(event.target.value)} disabled={isBrowserPreview || !displays.length}>
                      <option value="">Choose a monitor for this app run</option>
                      {displays.map(display => <option key={display.id} value={display.id}>{display.name} · {display.width} × {display.height} · ({display.x}, {display.y})</option>)}
                    </select>
                  </div>
                  <div className="spatial-window-actions">
                    <Button icon={RefreshCw} onClick={listDisplays} disabled={!!busy || isBrowserPreview}>Refresh monitors</Button>
                    <Button icon={Monitor} onClick={selectDisplay} disabled={!!busy || !displayChoice || isBrowserPreview}>Use this monitor</Button>
                  </div>
                  <SourceSummary source={currentSource} kind={selected.kind} />
                  <Note>{selected.displayId ? "A monitor reference is saved. Select it again for each app run or after a display change." : "Choose a connected monitor explicitly. Capture permission lasts only for this app run."} The ordinary Windows cursor is included. Hiding the overlay stops capture and desktop input.</Note>
                  <p className="selection-hint">Selecting a monitor sets the source resolution to its aspect ratio within capture limits. Custom dimensions stretch the full monitor to fill the overlay, keeping pointer positions aligned.</p>
                  <div className="spatial-shortcut-grid">
                    <div className="select-field">
                      <label htmlFor="desktop-key">Desktop visibility key</label>
                      <select id="desktop-key" value={desktopKey} onChange={event => edit({ shortcut: event.target.value ? desktopModifiers + event.target.value : "" })}>
                        <option value="">Unbound</option>
                        {Array.from({ length: 12 }, (_, i) => `F${i + 1}`).map(key => <option key={key} disabled={desktopModifiers + key === selected.precisionShortcut || menuShortcuts.includes(desktopModifiers + key)}>{key}</option>)}
                      </select>
                    </div>
                    <div className="select-field">
                      <label htmlFor="desktop-modifiers">Desktop key modifiers</label>
                      <select id="desktop-modifiers" value={desktopModifiers} disabled={!desktopKey} onChange={event => edit({ shortcut: event.target.value + desktopKey })}>
                        {MODIFIERS.map(value => <option key={value} value={value} disabled={value + desktopKey === selected.precisionShortcut || menuShortcuts.includes(value + desktopKey)}>{value ? value.slice(0, -1).replaceAll("+", " + ") : "None"}</option>)}
                      </select>
                    </div>
                  </div>
                  {shortcutError && <Note tone="amber">{shortcutError}</Note>}
                  <div className="select-field">
                    <label htmlFor="desktop-interaction">Desktop pointing mode</label>
                    <select id="desktop-interaction" value={selected.interaction || "mouse"} onChange={event => edit({ interaction: event.target.value })}>
                      <option value="mouse">Physical mouse</option><option value="head">Head pointing</option>
                    </select>
                  </div>
                  <Note>{(selected.interaction || "mouse") === "mouse" ? "Use your ordinary PC mouse." : "Head pointing controls the desktop cursor while this monitor is visible. Physical mouse activity pauses pointing; explicitly resume when ready."}</Note>
                  {selected.anchor === "head" && selected.interaction === "head" && <Note>Head pointing stays fixed on a head-following panel. Choose a local anchor to aim with your head, or use the PC mouse.</Note>}
                  {selectedStatus?.pointerMode && <p className="selection-hint">Active pointer: {selectedStatus.pointerMode}. {selectedStatus.inputError}</p>}
                  {selected.interaction === "head" && <>
                    <div className="spatial-window-actions">
                      {[ ["leftClick", "Left click"], ["rightClick", "Right click"], ["resume", "Resume pointing"] ].map(([action, label]) => <React.Fragment key={action}>
                        <Button disabled={!!busy || dirty || isBrowserPreview || selectedStatus?.status !== "capturing" || (action !== "resume" && (!selectedStatus?.inputAvailable || selectedStatus?.inputPaused))} onClick={() => run(`desktop-${action}`, () => api.desktopOverlayAction(profile.game, selected.id, action), result => result.message)}>{label}</Button>
                        <BindingButton actionId={`spatialOverlays.${selected.id}.${action}`} />
                      </React.Fragment>)}
                    </div>
                    {selectedStatus?.inputPaused && <Note tone="amber">Pointing paused after physical mouse activity. Resume pointing to return control to head pointing.</Note>}
                  </>}
                  <Range label="Precision zoom" value={Math.round((selected.precisionScale ?? 1.75) * 100)} min={100} max={300} step={5} onChange={value => edit({ precisionScale: value / 100 })} disabled={selected.interaction !== "head"} />
                  <div className="spatial-shortcut-grid">
                    <div className="select-field">
                      <label htmlFor="precision-key">Precision hold key</label>
                      <select id="precision-key" value={precisionKey} disabled={selected.interaction !== "head"} onChange={event => edit({ precisionShortcut: event.target.value ? precisionModifiers + event.target.value : "" })}>
                        <option value="">Unbound</option>
                        {Array.from({ length: 12 }, (_, i) => `F${i + 1}`).map(key => <option key={key} disabled={precisionModifiers + key === selected.shortcut || menuShortcuts.includes(precisionModifiers + key)}>{key}</option>)}
                      </select>
                    </div>
                    <div className="select-field">
                      <label htmlFor="precision-modifiers">Precision key modifiers</label>
                      <select id="precision-modifiers" value={precisionModifiers} disabled={selected.interaction !== "head" || !precisionKey} onChange={event => edit({ precisionShortcut: event.target.value + precisionKey })}>
                        {MODIFIERS.map(value => <option key={value} value={value} disabled={value + precisionKey === selected.shortcut || menuShortcuts.includes(value + precisionKey)}>{value ? value.slice(0, -1).replaceAll("+", " + ") : "None"}</option>)}
                      </select>
                    </div>
                  </div>
                  <div className="spatial-window-actions"><span>Hold precision</span><BindingButton actionId={`spatialOverlays.${selected.id}.precision`} /></div>
                  <Note>Hold the assigned key or controller button to enlarge this desktop around the current head-pointer target. Release to return to normal. Use your left/right click binding while held. Hiding the desktop, losing focus or using the physical mouse cancels precision.</Note>
                </div>
              ) : (
                <div className="spatial-window-picker">
                  <div className="select-field">
                    <label htmlFor="spatial-window">
                      Current desktop windows
                    </label>
                    <select
                      id="spatial-window"
                      value={windowChoice}
                      onChange={(event) => setWindowChoice(event.target.value)}
                      disabled={isBrowserPreview || !windows.length}
                    >
                      <option value="">Choose a window for this app run</option>
                      {windows.map((source) => (
                        <option key={source.id} value={source.id}>
                          {source.name}
                        </option>
                      ))}
                    </select>
                  </div>
                  <div className="spatial-window-actions">
                    <Button
                      icon={RefreshCw}
                      onClick={listWindows}
                      disabled={!!busy || isBrowserPreview}
                    >
                      Refresh windows
                    </Button>
                    <Button
                      icon={Monitor}
                      onClick={selectWindow}
                      disabled={!!busy || !windowChoice || isBrowserPreview}
                    >
                      Use this window
                    </Button>
                  </div>
                  <Note>
                    {selected.windowId
                      ? "A window reference is saved. Select the current window again for a new app run; a saved handle never grants capture by itself."
                      : "Choose a current window explicitly. Capture permission lasts only for this app run."}
                  </Note>
                  <SourceSummary source={currentSource} kind={selected.kind} />
                </div>
              )}
              {selected.kind === "url" && <SourceSummary source={currentSource} kind={selected.kind} />}
              <div className="panel-divider" />
              <div className="spatial-resolution-controls">
                <div className="select-field">
                  <label htmlFor="spatial-resolution">Source resolution</label>
                  <select
                    id="spatial-resolution"
                    value={`${selected.width}x${selected.height}`}
                    onChange={(event) => {
                      const [width, height] = event.target.value
                        .split("x")
                        .map(Number);
                      edit({ width, height });
                    }}
                  >
                    {!["640x360", "1024x576", "1280x720", "1920x1080"].includes(
                      `${selected.width}x${selected.height}`,
                    ) && (
                      <option value={`${selected.width}x${selected.height}`}>
                        Custom · {selected.width} × {selected.height}
                      </option>
                    )}
                    <option value="640x360">640 × 360</option>
                    <option value="1024x576">1024 × 576</option>
                    <option value="1280x720">1280 × 720</option>
                    <option value="1920x1080">1920 × 1080</option>
                  </select>
                </div>
                <NumberField
                  label="Source width"
                  min={256}
                  max={1920}
                  value={selected.width}
                  onChange={(width) => edit({ width })}
                />
                <NumberField
                  label="Source height"
                  min={128}
                  max={1080}
                  value={selected.height}
                  onChange={(height) => edit({ height })}
                />
              </div>
              <Range
                label="Requested source rate"
                min={1}
                max={selected.kind === "menu" ? 15 : 30}
                unit="Hz"
                value={selected.fps}
                onChange={(fps) => edit({ fps: Math.round(fps) })}
              />
              <div className="spatial-source-status">
                <span
                  className={`live-dot ${selectedStatus?.status === "capturing" ? "green" : ""}`}
                />
                <span>
                  {selectedStatus
                    ? STATUS_NAMES[selectedStatus.status] ||
                      selectedStatus.status
                    : "No saved runtime status for this source"}
                  {selectedStatus?.reason && ` · ${selectedStatus.reason}`}
                </span>
              </div>
            </Panel>
            <PlacementMap items={settings.items} selected={selected} onSelect={setSelectedId} onChange={updateItem} disabled={Boolean(busy)} profileKey={profile.id} />
            <CenteringControls
              profile={profile}
              gameName={gameName}
              centering={centering}
              onChange={onCenterChange}
              run={run}
              dirty={dirty}
              busy={busy}
            />
          </>
        )}
      </div>
      <div className="settings-stack">
        <Panel title="Rendering budget" icon={Settings2}>
          <Range
            label="Source pixel budget"
            min={15}
            max={180}
            unit="MP/s"
            value={settings.pixelBudgetMP}
            onChange={(pixelBudgetMP) =>
              onChange({ pixelBudgetMP: Math.round(pixelBudgetMP) })
            }
          />
          <div className="spatial-budget-summary">
            <div>
              <span>CONFIGURED SOURCES</span>
              <strong>
                {
                  settings.items.filter(
                    (item) =>
                      settings.enabled && item.enabled && item.opacity > 0,
                  ).length
                }
                <small> / 15</small>
              </strong>
            </div>
            <div>
              <span>ESTIMATED BUDGET</span>
              <strong>
                {configuredMP.toFixed(1)}
                <small> MP/s</small>
              </strong>
            </div>
          </div>
          <Note>
            Higher resolution and source rate use more resources. Rates are
            reduced to fit this budget; runtime composition limits may admit
            fewer than 15 overlays.
          </Note>
        </Panel>
        <Panel
          title="Actual overlay status"
          icon={Layers}
          action={
            <button
              className="icon-button"
              title="Refresh overlay status"
              aria-label="Refresh overlay status"
              onClick={refresh}
            >
              <RefreshCw size={14} />
            </button>
          }
        >
          <div className="engine-status">
            <span className={`live-dot ${status?.available ? "green" : ""}`} />
            <div>
              <strong>
                {status?.available
                  ? "Overlay host available"
                  : status?.running
                    ? "Host starting"
                    : "Overlay host offline"}
              </strong>
              <p>
                {statusError || status?.message || "Reading overlay status…"}
              </p>
            </div>
          </div>
          <dl className="detail-list">
            <div>
              <dt>Runtime capacity</dt>
              <dd>
                {status?.available
                  ? `${status.capacity} slots`
                  : "Not reported"}
              </dd>
            </div>
            <div>
              <dt>Active overlays</dt>
              <dd>{status?.available ? status.activeOverlayCount : "--"}</dd>
            </div>
          </dl>
          <Button
            icon={Crosshair}
            onClick={() =>
              run(
                "overlay-recenter",
                () => api.recenterOverlays(profile.game),
                (result) => result.message,
              )
            }
            disabled={!!busy || !status?.available || isBrowserPreview}
          >
            Recenter overlay anchors
          </Button>
          <Note>
            Only fixed play-space anchors move. The game camera stays unchanged.
            Runtime status reflects the saved active game profile.
          </Note>
        </Panel>
        {selected && (
          <Panel title="Position & anchoring" icon={Move}>
            {selected.kind !== "menu" && <Segmented
              label="Overlay anchor"
              value={selected.anchor}
              onChange={(anchor) => edit({ anchor })}
              options={[
                { value: "local", label: "Play space" },
                { value: "head", label: "Head-locked" },
              ]}
            />}
            <div
              className="spatial-presets"
              role="group"
              aria-label="Placement presets"
            >
              {Object.entries(PRESETS).map(([name, values]) => (
                <button key={name} onClick={() => edit(values)}>
                  {name[0].toUpperCase() + name.slice(1)}
                </button>
              ))}
            </div>
            <Range
              label="Horizontal position"
              min={-5}
              max={5}
              step={0.05}
              unit="m"
              value={selected.x}
              onChange={(x) => edit({ x })}
            />
            <Range
              label="Vertical position"
              min={-5}
              max={5}
              step={0.05}
              unit="m"
              value={selected.y}
              onChange={(y) => edit({ y })}
            />
            <Range
              label="Distance forward"
              min={0.1}
              max={10}
              step={0.05}
              unit="m"
              value={-selected.z}
              onChange={(distance) => edit({ z: -distance })}
            />
            <Range
              label="Overlay width"
              min={0.1}
              max={3}
              step={0.05}
              unit="m"
              value={selected.widthMeters}
              onChange={(widthMeters) => edit({ widthMeters })}
            />
            <Range
              label="Overlay opacity"
              min={0}
              max={100}
              value={selected.opacity}
              onChange={(opacity) => edit({ opacity })}
            />
            <details className="spatial-rotation">
              <summary>Rotation</summary>
              <Range
                label="Overlay yaw"
                min={-180}
                max={180}
                unit="°"
                value={selected.yaw}
                onChange={(yaw) => edit({ yaw })}
              />
              <Range
                label="Overlay pitch"
                min={-180}
                max={180}
                unit="°"
                value={selected.pitch}
                onChange={(pitch) => edit({ pitch })}
              />
              <Range
                label="Overlay roll"
                min={-180}
                max={180}
                unit="°"
                value={selected.roll}
                onChange={(roll) => edit({ roll })}
              />
            </details>
            <Note>
              Positive X moves right; positive Y moves up. Save the profile to
              apply placement. The map is illustrative.
            </Note>
          </Panel>
        )}
        {!selected && (
          <CenteringControls
            profile={profile}
            gameName={gameName}
            centering={centering}
            onChange={onCenterChange}
            run={run}
            dirty={dirty}
            busy={busy}
          />
        )}
      </div>
    </div>
  );
}

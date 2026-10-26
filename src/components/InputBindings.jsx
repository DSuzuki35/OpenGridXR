import React, {
  createContext,
  useContext,
  useEffect,
  useRef,
  useState,
} from "react";
import { Gamepad2, RefreshCw, Trash2, X } from "lucide-react";
import { api, isBrowserPreview } from "../api.mjs";
import {
  bindingDefaults,
  listToggleActions,
  validateBindings,
} from "../input-bindings.mjs";

const Context = createContext(null);
export const useInputBindings = () => useContext(Context);
const DIRECTIONS = [
  "Up",
  "Up-right",
  "Right",
  "Down-right",
  "Down",
  "Down-left",
  "Left",
  "Up-left",
];
export function describeInput(binding) {
  return binding.kind === "pov"
    ? `POV ${binding.index + 1} ${DIRECTIONS[binding.direction]}`
    : `Button ${binding.index + 1}`;
}
export function BindingButton({ actionId, disabled = false }) {
  const context = useInputBindings();
  if (!context || !actionId) return null;
  const action = context.actions.find((item) => item.id === actionId);
  if (!action) return null;
  const binding = context.settings.bindings.find(
    (item) => item.actionId === actionId,
  );
  return (
    <button
      type="button"
      className={`binding-button ${binding ? "bound" : ""}`}
      aria-label={`Bind controller: ${action?.label || actionId}`}
      title={
        binding
          ? `${binding.deviceName} - ${describeInput(binding)}. Click to relearn.`
          : "Bind a wheel, joystick or buttonbox input"
      }
      disabled={
        disabled || context.busy || action?.disabled || isBrowserPreview
      }
      onClick={() => context.learn(actionId)}
    >
      <Gamepad2 size={13} />
    </button>
  );
}
export function InputBindingsProvider({
  profile,
  dirty,
  busy,
  onChange,
  onToggle,
  children,
}) {
  const actions = listToggleActions(profile);
  const savedSettings = { ...bindingDefaults(), ...profile.inputBindings };
  const settings = {
    ...savedSettings,
    bindings: savedSettings.bindings.filter((binding) =>
      actions.some((action) => action.id === binding.actionId),
    ),
  };
  const inactiveCount = savedSettings.bindings.length - settings.bindings.length;
  const [capture, setCapture] = useState(null);
  const [captureError, setCaptureError] = useState("");
  const current = useRef({ profile, settings, onChange });
  current.current = { profile, settings, onChange };
  const session = useRef(0);
  const close = async () => {
    session.current++;
    setCapture(null);
    setCaptureError("");
    try {
      await api.inputCaptureCancel();
    } catch {}
  };
  const learn = async (actionId) => {
    if (
      busy ||
      !actions.some((action) => action.id === actionId && !action.disabled)
    )
      return;
    const token = ++session.current;
    setCaptureError("");
    setCapture({ state: "starting", actionId, profileId: profile.id });
    try {
      const result = await api.inputCaptureStart(actionId, profile.id);
      if (session.current === token)
        setCapture({ ...result, actionId, profileId: profile.id });
    } catch (error) {
      if (session.current === token) {
        setCapture({ state: "error", actionId, profileId: profile.id });
        setCaptureError(error.message);
      }
    }
  };
  useEffect(() => {
    session.current++;
    setCapture(null);
    setCaptureError("");
    api.inputCaptureCancel?.().catch(() => {});
  }, [profile.id]);
  useEffect(() => {
    if (capture?.state !== "listening") return;
    let disposed = false,
      timer;
    const token = session.current;
    const poll = async () => {
      try {
        const result = await api.inputCaptureStatus();
        if (disposed || session.current !== token) return;
        if (result.state === "captured") {
          if (
            result.profileId !== current.current.profile.id ||
            result.actionId !== capture.actionId
          )
            throw new Error(
              "The capture belongs to another profile. Try again.",
            );
          const input = result.input;
          const descriptor = {
            actionId: capture.actionId,
            deviceId: input.deviceId,
            deviceName: input.deviceName,
            kind: input.kind,
            index: input.index,
            ...(input.kind === "pov" ? { direction: input.direction } : {}),
          };
          const next = {
            ...current.current.settings,
            bindings: [
              ...current.current.settings.bindings.filter(
                (item) => item.actionId !== capture.actionId,
              ),
              descriptor,
            ],
          };
          validateBindings(next, current.current.profile);
          current.current.onChange(next);
          setCapture({ ...result, input: descriptor });
          return;
        }
        if (result.state !== "listening") {
          setCapture(result);
          setCaptureError(
            result.error ||
              "Capture ended. Try again when your device is ready.",
          );
          return;
        }
      } catch (error) {
        if (!disposed) {
          setCaptureError(error.message);
          setCapture({ ...capture, state: "error" });
          api.inputCaptureCancel?.().catch(() => {});
        }
        return;
      }
      if (!disposed) timer = setTimeout(poll, 250);
    };
    timer = setTimeout(poll, 250);
    return () => {
      disposed = true;
      clearTimeout(timer);
    };
  }, [capture?.state, capture?.actionId, capture?.profileId]);
  useEffect(
    () => () => {
      session.current++;
      api.inputCaptureCancel?.().catch(() => {});
    },
    [],
  );
  const remove = (actionId) =>
    onChange({
      ...settings,
      bindings: settings.bindings.filter((item) => item.actionId !== actionId),
    });
  const activeAction = actions.find(
    (action) => action.id === capture?.actionId,
  );
  return (
    <Context.Provider
      value={{
        profile,
        settings,
        actions,
        inactiveCount,
        dirty,
        busy,
        learn,
        remove,
        toggle: onToggle,
        setEnabled: (enabled) => onChange({ ...settings, enabled }),
      }}
    >
      <div style={{ display: "contents" }} inert={!!capture}>
        {children}
      </div>
      {capture && (
        <div className="modal-backdrop binding-backdrop" role="presentation">
          <section
            className="modal binding-modal"
            role="dialog"
            aria-modal="true"
            aria-label="Bind controller input"
            onKeyDown={(event) => {
              if (event.key === "Escape") {
                event.stopPropagation();
                void close();
              }
            }}
          >
            <div className="binding-modal-heading">
              <Gamepad2 size={24} />
              <button
                className="icon-button"
                aria-label="Close binding capture"
                autoFocus
                onClick={close}
              >
                <X size={17} />
              </button>
            </div>
            <span className="eyebrow">CONTROLLER BINDING</span>
            <h2>{activeAction?.label || "Controller input"}</h2>
            {capture.state === "captured" ? (
              <>
                <div className="binding-captured">
                  <strong>{capture.input.deviceName}</strong>
                  <span>{describeInput(capture.input)}</span>
                </div>
                <p>
                  Binding added to your draft. Save the profile to use it while
                  driving.
                </p>
              </>
            ) : (
              <>
                <p>
                  Release the control, then press a wheel, joystick or buttonbox
                  button or POV direction. Capture waits for a new press for up
                  to 15 seconds.
                </p>
                <div
                  className={`binding-capture-state ${captureError ? "error" : ""}`}
                  role="status"
                >
                  {captureError ||
                    (capture.state === "starting"
                      ? "Starting input capture..."
                      : "Listening for one input...")}
                </div>
                <p className="selection-hint">
                  Learning replaces this action's current binding. A control
                  already assigned to another action is not reassigned
                  automatically.
                </p>
              </>
            )}
            {activeAction?.restartRequired && (
              <p className="binding-restart-note">
                This setting can still require an XR session restart. Binding a
                button does not remove that requirement.
              </p>
            )}
            <div className="binding-modal-actions">
              {settings.bindings.some(
                (item) => item.actionId === capture.actionId,
              ) && (
                <button
                  className="button secondary"
                  onClick={() => {
                    remove(capture.actionId);
                    void close();
                  }}
                >
                  <Trash2 size={14} />
                  Remove binding
                </button>
              )}
              {capture.state === "error" && (
                <button
                  className="button secondary"
                  onClick={() => learn(capture.actionId)}
                >
                  <RefreshCw size={14} />
                  Try again
                </button>
              )}
              <button className="button primary" onClick={close}>
                {capture.state === "captured" ? "Done" : "Cancel capture"}
              </button>
            </div>
          </section>
        </div>
      )}
    </Context.Provider>
  );
}

export function InputBindingsPage() {
  const context = useInputBindings();
  const [status, setStatus] = useState(null),
    [error, setError] = useState("");
  useEffect(() => {
    let disposed = false,
      timer;
    const poll = async () => {
      try {
        const next = await api.inputStatus();
        if (!disposed) {
          setStatus(next);
          setError("");
        }
      } catch (error) {
        if (!disposed) setError(error.message);
      }
      if (!disposed) timer = setTimeout(poll, 1000);
    };
    void poll();
    return () => {
      disposed = true;
      clearTimeout(timer);
    };
  }, []);
  if (!context) return null;
  const { settings, actions, dirty, profile } = context;
  const devices = status?.devices || [];
  return (
    <div className="bindings-layout">
      <section className="panel">
        <div className="panel-heading">
          <h2>
            <Gamepad2 size={16} />
            Profile bindings
          </h2>
          <span className="badge subtle">
            {settings.bindings.length} / 128 INPUTS
          </span>
        </div>
        <p className="panel-description">
          Bind driving quick actions to a wheel, joystick or buttonbox. Actions
          work on hidden tabs while OpenGrid XR is running and are paused while
          the editor has unsaved changes.
        </p>
        {context.inactiveCount > 0 && (
          <p className="selection-hint" role="status">
            {context.inactiveCount} older setup binding(s) are inactive. Editing
            your bindings removes those assignments; profile settings are kept.
          </p>
        )}
        <div className="binding-action-list">
          {actions.map((action) => {
            const bound = settings.bindings.filter(
              (item) => item.actionId === action.id,
            );
            return (
              <div className="binding-action-row" key={action.id}>
                <div>
                  <small>{action.group}</small>
                  <strong>{action.label}</strong>
                  <span>
                    {bound.length
                      ? bound
                          .map(
                            (binding) =>
                              `${binding.deviceName} - ${describeInput(binding)}`,
                          )
                          .join("; ")
                      : "Not bound"}
                    {action.restartRequired
                      ? " - XR restart may be required"
                      : ""}
                  </span>
                </div>
                <BindingButton
                  actionId={action.id}
                  disabled={action.disabled}
                />
                {bound.length > 0 && (
                  <button
                    className="icon-button"
                    aria-label={`Remove binding: ${action.label}`}
                    onClick={() => context.remove(action.id)}
                  >
                    <Trash2 size={14} />
                  </button>
                )}
              </div>
            );
          })}
        </div>
      </section>
      <div className="settings-stack">
        <section className="panel">
          <div className="panel-heading">
            <h2>Background input</h2>
          </div>
          <div className="engine-status">
            <span
              className={`live-dot ${status?.running && !status?.paused && !dirty ? "green" : ""}`}
            />
            <div>
              <strong>
                {dirty
                  ? "Paused for unsaved changes"
                  : !settings.enabled
                    ? "Paused by profile"
                    : status?.paused
                      ? "Actions paused"
                      : status?.running
                        ? "Input reader running"
                        : "Input reader idle"}
              </strong>
              <p>{error || status?.message || "Checking input reader..."}</p>
            </div>
          </div>
          <BindingMaster settings={settings} />
          <p className="selection-hint">
            Save this preference with your profile. No input reader is needed
            when there are no active bindings or capture requests.
          </p>
          <p className="binding-restart-note">
            HUD and frame generation bindings control the running XR session.
            Enable these features in your saved profile before starting the game.
            Physical-device operation must be checked with your own hardware.
          </p>
        </section>
        <section className="panel">
          <div className="panel-heading">
            <h2>Connected controllers</h2>
          </div>
          {devices.length ? (
            devices.map((device) => (
              <div className="binding-device" key={device.id}>
                <Gamepad2 size={17} />
                <div>
                  <strong>{device.name}</strong>
                  <span>
                    {device.buttons} buttons - {device.povs} POV hats
                  </span>
                </div>
              </div>
            ))
          ) : (
            <p className="panel-description">
              {status?.running
                ? "No compatible controllers reported."
                : "Devices are listed while bindings or capture need the reader."}
            </p>
          )}
          <p className="selection-hint">
            The reader uses nonexclusive background input. It does not control
            force feedback or send button presses to the game.
          </p>
        </section>
      </div>
    </div>
  );
}
function BindingMaster({ settings }) {
  const context = useInputBindings();
  return (
    <button
      className="button secondary"
      onClick={() => context.setEnabled(!settings.enabled)}
    >
      {settings.enabled ? "Pause profile bindings" : "Resume profile bindings"}
    </button>
  );
}

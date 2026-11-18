import {effectiveMask} from "../model.mjs";
import { occupiedShortcuts } from "../spatial-overlays.mjs";
import React, { useId } from "react";
import {
  Activity,
  Check,
  Eye,
  Gauge,
  Keyboard,
  Monitor,
  Palette,
  RefreshCw,
} from "lucide-react";
import { Button, Note, Panel, Range, Segmented, Toggle } from "./Controls.jsx";
import { TrackArt } from "./ContourEditor.jsx";

const ACCENTS = {
  violet: "#8e70f2",
  teal: "#4cb5a6",
  copper: "#cc9168",
  blue: "#669ee7",
  rose: "#cc7eaa",
};
const SHORTCUTS = Array.from(
  { length: 12 },
  (_, index) => `Ctrl+Alt+F${index + 1}`,
);

function SelectField({ label, value, onChange, options, description }) {
  const id = useId();
  return (
    <div className="select-field">
      <label htmlFor={id}>{label}</label>
      <select
        id={id}
        value={value}
        onChange={(event) => onChange(event.target.value)}
      >
        {options.map((option) => (
          <option key={option.value} value={option.value}>
            {option.label}
          </option>
        ))}
      </select>
      {description && <p className="selection-hint">{description}</p>}
    </div>
  );
}

function Shortcut({ label, value, onChange, occupied }) {
  const id = useId();
  return (
    <div className="select-field">
      <label htmlFor={id}>{label}</label>
      <select
        id={id}
        value={value}
        onChange={(event) => {
          if (!event.target.value || !occupied.includes(event.target.value))
            onChange(event.target.value);
        }}
      >
        <option value="">Disabled</option>
        {SHORTCUTS.map((shortcut) => (
          <option
            key={shortcut}
            value={shortcut}
            disabled={occupied.includes(shortcut)}
          >
            {shortcut.replaceAll("+", " + ")}
            {occupied.includes(shortcut) ? " · in use" : ""}
          </option>
        ))}
      </select>
    </div>
  );
}

export function EngineStatus({ runtime }) {
  const session = runtime.active ? runtime.session : null;
  const loaded = session?.engineLoaded === true;
  const error = session?.engineError;
  return (
    <div className="engine-status">
      <span className={`live-dot ${loaded ? "green" : error ? "amber" : ""}`} />
      <div>
        <strong>
          {error
            ? "Engine reported an error"
            : loaded
              ? "Engine loaded"
              : runtime.active
                ? "Engine not confirmed"
                : "Awaiting an XR session"}
        </strong>
        <p>
          {error ||
            (loaded
              ? "The game reports that the enhancement engine is loaded."
              : "Engine status updates during an active game session.")}
        </p>
      </div>
    </div>
  );
}

function OverlayPreview({ overlay, profile, gameName }) {
  const accent =
    overlay.accent === "custom" ? overlay.customColor : ACCENTS[overlay.accent];
  const compact = overlay.layout === "compact";
  const fps = !compact || ["all", "fps"].includes(overlay.compactMetrics);
  const frameTime =
    !compact || ["all", "frameTime"].includes(overlay.compactMetrics);
  const scale = (overlay.scale / 100) * (1.2 / overlay.distanceMeters) ** 0.35;
  return (
    <Panel
      title="Your in-headset dashboard"
      icon={Eye}
      action={<span className="badge subtle">ILLUSTRATIVE PREVIEW</span>}
      className="overlay-preview-panel"
    >
      <div className="overlay-preview-zone">
        <svg
          className="overlay-scene"
          viewBox="0 0 800 450"
          preserveAspectRatio="xMidYMid slice"
          aria-hidden="true"
        >
          <TrackArt id="overlay-art" />
        </svg>
        <div className="overlay-reticle" />
        <div
          className={`osd-preview ${compact ? "compact-layout" : ""}`}
          style={{
            "--osd-accent": accent,
            opacity: overlay.opacity / 100,
            left: `${50 + (overlay.horizontalDegrees / 40) * 16}%`,
            top: `${50 - (overlay.verticalDegrees / 35) * 18}%`,
            transform: `translate(-50%,-50%) scale(${scale})`,
          }}
        >
          <div className="osd-title">
            <span className="osd-mark" />
            <strong>OPENGRID XR</strong>
            <span>{overlay.showClock ? "--:--" : "PREVIEW"}</span>
          </div>
          {overlay.showPerformance && (fps || frameTime) && (
            <div className="osd-metrics">
              {fps && (
                <>
                  <div>
                    <span>APP FPS</span>
                    <strong className="overlay-metric-value">--</strong>
                  </div>
                  <div>
                    <span>HEADSET FPS</span>
                    <strong className="overlay-metric-value">--</strong>
                  </div>
                </>
              )}
              {frameTime && (
                <>
                  <div>
                    <span>CPU FRAME</span>
                    <strong className="overlay-metric-value">
                      --<small>ms</small>
                    </strong>
                  </div>
                  <div>
                    <span>GPU FRAME</span>
                    <strong className="overlay-metric-value">
                      --<small>ms</small>
                    </strong>
                  </div>
                </>
              )}
            </div>
          )}
          {!compact && overlay.showGraph && (
            <div className="osd-graph">
              <div />
              <span>No live samples</span>
            </div>
          )}
          {overlay.showResolution !== false && (
            <div className="osd-details">
              <div>
                <span>Rendered / eye</span>
                <strong>Unavailable</strong>
              </div>
              <div>
                <span>Configured render scale</span>
                <strong>{profile.view.renderScale}%</strong>
              </div>
            </div>
          )}
          {!compact && (
            <div className="osd-details">
              {overlay.showRuntime && (
                <div>
                  <span>Runtime</span>
                  <strong>--</strong>
                </div>
              )}
              {overlay.showApp && (
                <div>
                  <span>Application</span>
                  <strong>{gameName}</strong>
                </div>
              )}
              {overlay.showToolkit && (
                <>
                  <div className="osd-section-label">PROFILE SETTINGS</div>
                  <div>
                    <span>Supersampling</span>
                    <strong>{profile.view.renderScale}%</strong>
                  </div>
                  <div>
                    <span>Crop L / R / T / B</span>
                    <strong>
                      {["left", "right", "top", "bottom"]
                        .map((edge) => profile.view.crop[edge])
                        .join(" / ")}
                      %
                    </strong>
                  </div>
                  <div>
                    <span>Contour</span>
                    <strong>
                      {effectiveMask(profile)
                        ? "On"
                        : "Off"}{" "}
                    </strong>
                  </div>
                </>
              )}
            </div>
          )}
          <div className="osd-footer">
            CONFIGURATION PREVIEW · METRICS UNAVAILABLE
          </div>
        </div>
        <span className="overlay-zone-caption">
          ORIGINAL FIT GUIDE · PLACEMENT IS APPROXIMATE
        </span>
      </div>
      <div className="overlay-preview-caption">
        <span className={`live-dot ${overlay.enabled ? "violet" : ""}`} />
        <strong>
          {overlay.enabled
            ? "Configured for your next session"
            : "Previewing · disabled in this profile"}
        </strong>
        <p>
          Live FPS and frame times come from the game. This preview uses
          placeholders.
        </p>
      </div>
    </Panel>
  );
}

export function OverlayPage({
  overlay,
  profile,
  gameName,
  onChange,
  runtime,
  onRefresh,
}) {
  return (
    <div className="enhancement-layout overlay-layout">
      <div className="enhancement-main">
        <OverlayPreview
          overlay={overlay}
          profile={profile}
          gameName={gameName}
        />
        <div className="enhancement-pair">
          <Panel title="Contents" icon={Gauge}>
            <div className="toggle-list">
              {[
                { key: "showPerformance", label: "Show FPS & frame times" },
                { key: "showResolution", label: "Show render resolution & scale" },
                { key: "showGraph", label: "Show frame-time graph" },
                { key: "showRuntime", label: "Show runtime" },
                { key: "showApp", label: "Show application" },
                { key: "showClock", label: "Show clock" },
                { key: "showToolkit", label: "Show toolkit settings" },
              ].map(({ key, label }) => (
                <Toggle
                  key={key}
                  bindingAction={`overlay.${key}`}
                  label={label}
                  checked={key === "showResolution" ? overlay[key] !== false : overlay[key]}
                  onChange={(value) => onChange({ [key]: value })}
                />
              ))}
            </div>
            <SelectField
              label="Compact metrics"
              value={overlay.compactMetrics}
              onChange={(compactMetrics) => onChange({ compactMetrics })}
              options={[
                { value: "all", label: "FPS & frame times" },
                { value: "fps", label: "FPS only" },
                { value: "frameTime", label: "Frame times only" },
                { value: "none", label: "Header only" },
              ]}
              description="Compact layout hides the graph and detail rows, preserving your content preferences."
            />
          </Panel>
          <Panel title="Color & shortcuts" icon={Palette}>
            <div
              className="accent-swatches"
              role="group"
              aria-label="Overlay accent color"
            >
              {Object.entries(ACCENTS).map(([name, color]) => (
                <button
                  key={name}
                  aria-label={`${name[0].toUpperCase() + name.slice(1)} accent`}
                  aria-pressed={overlay.accent === name}
                  title={name}
                  className={overlay.accent === name ? "selected" : ""}
                  style={{ "--swatch": color }}
                  onClick={() => onChange({ accent: name })}
                >
                  {overlay.accent === name && <Check size={13} />}
                </button>
              ))}
              <button
                className={`custom-accent ${overlay.accent === "custom" ? "selected" : ""}`}
                aria-pressed={overlay.accent === "custom"}
                onClick={() => onChange({ accent: "custom" })}
              >
                Custom
              </button>
            </div>
            {overlay.accent === "custom" && (
              <label className="custom-color-field">
                Custom overlay color
                <input
                  aria-label="Custom overlay color"
                  type="color"
                  value={overlay.customColor}
                  onChange={(event) =>
                    onChange({ customColor: event.target.value })
                  }
                />
                <span>{overlay.customColor}</span>
              </label>
            )}
            <Shortcut
              label="Overlay toggle shortcut"
              value={overlay.toggleShortcut}
              onChange={(toggleShortcut) => onChange({ toggleShortcut })}
              occupied={occupiedShortcuts(profile, "overlay", "toggleShortcut")}
            />
            <Shortcut
              label="Overlay layout shortcut"
              value={overlay.cycleShortcut}
              onChange={(cycleShortcut) => onChange({ cycleShortcut })}
              occupied={occupiedShortcuts(profile, "overlay", "cycleShortcut")}
            />
            <Note>
              Toggle the panel and cycle its layout without leaving the headset.
            </Note>
          </Panel>
        </div>
      </div>
      <div className="settings-stack">
        <Panel title="Head-locked panel" icon={Monitor}>
          <Toggle
            label="Enable performance HUD"
            bindingAction="overlay.enabled"
            checked={overlay.enabled}
            onChange={(enabled) => onChange({ enabled })}
            description="Save and restart your XR session"
          />
          <div className="panel-divider" />
          <Segmented
            label="Overlay layout"
            value={overlay.layout}
            onChange={(layout) => onChange({ layout })}
            options={[
              { value: "expanded", label: "Expanded" },
              { value: "compact", label: "Compact" },
            ]}
          />
          <Range
            label="Horizontal angle"
            unit="°"
            min={-40}
            max={40}
            value={overlay.horizontalDegrees}
            onChange={(horizontalDegrees) => onChange({ horizontalDegrees })}
          />
          <Range
            label="Vertical angle"
            unit="°"
            min={-35}
            max={35}
            value={overlay.verticalDegrees}
            onChange={(verticalDegrees) => onChange({ verticalDegrees })}
          />
          <Range
            label="Panel distance"
            unit="m"
            min={0.5}
            max={3}
            step={0.1}
            value={overlay.distanceMeters}
            onChange={(distanceMeters) => onChange({ distanceMeters })}
          />
          <Range
            label="Panel size"
            min={25}
            max={150}
            value={overlay.scale}
            onChange={(scale) => onChange({ scale })}
          />
          <Range
            label="Panel opacity"
            min={30}
            max={100}
            value={overlay.opacity}
            onChange={(opacity) => onChange({ opacity: Math.round(opacity) })}
          />
          <Range
            label="Update frequency"
            unit="Hz"
            min={1}
            max={20}
            value={overlay.updateHz}
            onChange={(updateHz) =>
              onChange({ updateHz: Math.round(updateHz) })
            }
          />
        </Panel>
        <Panel title="Actual engine status" icon={Activity}>
          <EngineStatus runtime={runtime} />
          <Button icon={RefreshCw} onClick={onRefresh}>
            Refresh status
          </Button>
        </Panel>
        <Note tone="violet">
          The overlay follows your head. Your profile stores its position,
          appearance, visible toolkit settings and shortcuts.
        </Note>
      </div>
    </div>
  );
}

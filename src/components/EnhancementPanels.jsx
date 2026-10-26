import React, { useId } from "react";
import {
  Activity,
  ArrowUpRight,
  Check,
  Cpu,
  Eye,
  Gauge,
  Keyboard,
  Layers,
  Monitor,
  Palette,
  RefreshCw,
  Settings2,
  ShieldCheck,
  Sparkles,
} from "lucide-react";
import { Button, Note, Panel, Range, Segmented, Toggle } from "./Controls.jsx";
import { TrackArt } from "./ContourEditor.jsx";
import "./FrameGeneration.css";

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
              ? "The game reports the engine loaded. Frame generation activity is not confirmed by this status."
              : "This status updates from an actual game session. Profile settings alone do not confirm frame generation is running.")}
        </p>
      </div>
    </div>
  );
}

function SynthesisPreview({ mode }) {
  const extrapolate = mode === "extrapolate";
  const frames = extrapolate
    ? [
        { time: "t", label: "Previous frame", position: "20%" },
        { time: "t + 1", label: "Latest frame", position: "55%" },
        { time: "t + 1.5", label: "Projected frame", position: "73%", generated: true },
      ]
    : [
        { time: "t", label: "Earlier frame", position: "20%" },
        { time: "t + 0.5", label: "In-between frame", position: "38%", generated: true },
        { time: "t + 1", label: "Later frame", position: "55%" },
      ];
  return (
    <div className={`synthesis-preview ${extrapolate ? "extrapolate" : "interpolate"}`}>
      <div className="synthesis-preview-heading" aria-live="polite">
        <strong>{extrapolate ? "Extrapolation" : "Interpolation"}</strong>
        <span>{extrapolate ? "Projected beyond the latest frame" : "Between two known frames"}</span>
      </div>
      <div className="frame-sequence" aria-label={extrapolate
        ? "Two known rendered frames followed by a generated prediction"
        : "A generated frame between two known rendered frames"}>
        {frames.map((frame, index) => (
          <React.Fragment key={`${mode}-${frame.time}`}>
            {index > 0 && <span className="sequence-connector" aria-hidden="true">{!extrapolate && index === 2 ? "←" : "→"}</span>}
            <div className={`sequence-frame ${frame.generated ? "generated" : "original"}`} data-frame-kind={frame.generated ? "generated" : "rendered"}>
              <span className="frame-time">{frame.time}</span>
              <div className="frame-road" />
              <div className="frame-car" style={{ left: frame.position }} aria-hidden="true" />
              <strong>{frame.label}<small>{frame.generated ? "GENERATED" : "RENDERED · KNOWN"}</small></strong>
              {frame.generated && <span className="sequence-spark"><Sparkles size={17} /></span>}
            </div>
          </React.Fragment>
        ))}
      </div>
      <div className="frame-motion-track" aria-hidden="true"><span className="frame-motion-dot" /></div>
      <p className="synthesis-explanation">{extrapolate
        ? "Estimate motion from the previous and latest frames, then project it forward into a new frame."
        : "Use motion between an earlier and a later rendered frame to generate a frame between them."}</p>
      <div className="diagram-caption">ILLUSTRATION ONLY · NO MEASURED FPS OR LATENCY</div>
    </div>
  );
}

export function FrameGenerationPage({
  framegen,
  overlay,
  centeringShortcut = "",
  onChange,
  runtime,
  onRefresh,
  onSetup,
  onOpenVectorXR,
}) {
  return (
    <div className="enhancement-layout">
      <div className="enhancement-main">
        <Panel
          title="Frame generation in your headset"
          icon={Sparkles}
          action={<span className="badge">EXPERIMENTAL</span>}
          className="framegen-intro"
        >
          <div className="vectorxr-credit">
            <div>
              <span className="vectorxr-credit-kicker">ORIGINAL ENGINE · DIENERTECH LLC</span>
              <h3>Derived from VectorXR</h3>
              <p>OpenGrid XR adapts VectorXR’s open-source frame generation engine, developed by DienerTech LLC, under MPL-2.0.</p>
            </div>
            <Button variant="ghost" icon={ArrowUpRight} onClick={onOpenVectorXR}>VectorXR original project</Button>
          </div>
          <div className="feature-enable">
            <div>
              <h3>A fuller motion sequence.</h3>
              <p>
                Configure additional headset frames using the included engine.
              </p>
            </div>
            <Toggle
              label="Configure frame generation"
              bindingAction="framegen.enabled"
              checked={framegen.enabled}
              onChange={(enabled) => onChange({ enabled })}
            />
          </div>
          <SynthesisPreview mode={framegen.synthesisMode} />
          <div className="feature-state">
            <span>PROFILE CONFIGURATION</span>
            <strong>
              {framegen.enabled ? "Enabled for next session" : "Off"}
            </strong>
            <span>Save, then restart the game to apply.</span>
          </div>
        </Panel>
        <div className="enhancement-pair">
          <Panel title="Synthesis" icon={Layers}>
            <SelectField
              label="Synthesis mode"
              value={framegen.synthesisMode}
              onChange={(synthesisMode) => onChange({ synthesisMode })}
              options={[
                { value: "interpolate", label: "Interpolation" },
                { value: "extrapolate", label: "Extrapolation" },
              ]}
              description="Interpolation combines surrounding frames; extrapolation projects motion forward."
            />
            <SelectField
              label="Frame multiplier"
              value={framegen.frameMultiplier}
              onChange={(frameMultiplier) => onChange({ frameMultiplier })}
              options={[
                { value: "dynamic", label: "Dynamic" },
                { value: "auto", label: "Automatic" },
                { value: "2x", label: "2×" },
                { value: "3x", label: "3×" },
                { value: "4x", label: "4×" },
              ]}
              description="A requested synthesis setting, not a measured FPS multiplier."
            />
            <SelectField
              label="Optical flow quality"
              value={framegen.flowQuality}
              onChange={(flowQuality) => onChange({ flowQuality })}
              options={[
                { value: "performance", label: "Performance" },
                { value: "balanced", label: "Balanced" },
                { value: "quality", label: "Quality" },
              ]}
            />
          </Panel>
          <Panel title="Motion & artifacts" icon={Settings2}>
            <SelectField
              label="Motion estimation"
              value={framegen.motionEstimation}
              onChange={(motionEstimation) => onChange({ motionEstimation })}
              options={[
                { value: "shader", label: "Shader" },
                {
                  value: "hardware",
                  label: "NVIDIA hardware · shader fallback",
                },
              ]}
              description="Hardware optical flow is optional. The shader path is used when hardware support is unavailable."
            />
            <Range
              label="Artifact suppression"
              value={framegen.artifactSuppression}
              onChange={(artifactSuppression) =>
                onChange({
                  artifactSuppression: Math.round(artifactSuppression),
                })
              }
            />
            <Range
              label="Motion limit"
              min={2}
              max={25}
              value={framegen.motionLimitPercent}
              onChange={(motionLimitPercent) =>
                onChange({ motionLimitPercent: Math.round(motionLimitPercent) })
              }
            />
          </Panel>
        </div>
        <Note tone="amber">
          Turn off your runtime’s motion smoothing or reprojection before
          evaluating frame generation. Enable changes require a game restart;
          use the shortcut for an in-session A/B comparison after setup.
        </Note>
      </div>
      <div className="settings-stack">
        <Panel title="Actual engine status" icon={Activity}>
          <EngineStatus runtime={runtime} />
          <div className="engine-actions">
            <Button icon={RefreshCw} onClick={onRefresh}>
              Refresh status
            </Button>
            <Button variant="ghost" icon={ArrowUpRight} onClick={onSetup}>
              Setup
            </Button>
          </div>
          {runtime.updateRequired && (
            <Note tone="amber">
              An installed runtime update is required. Install or update the
              runtime in Setup.
            </Note>
          )}
        </Panel>
        <Panel title="Diagnostics & shortcut" icon={Keyboard}>
          <SelectField
            label="Diagnostic view"
            value={framegen.diagnosticView}
            onChange={(diagnosticView) => onChange({ diagnosticView })}
            options={[
              { value: "off", label: "Off" },
              { value: "flow", label: "Optical flow" },
              { value: "confidence", label: "Confidence" },
              { value: "tint", label: "Generated-frame tint" },
            ]}
          />
          <Shortcut
            label="Frame generation toggle shortcut"
            value={framegen.toggleShortcut}
            onChange={(toggleShortcut) => onChange({ toggleShortcut })}
            occupied={[
              overlay.toggleShortcut,
              overlay.cycleShortcut,
              centeringShortcut,
            ]}
          />
          <Note>
            Reserved shortcuts appear as “in use”. Assign each action a
            different key combination.
          </Note>
        </Panel>
        <Panel title="One toolkit. Included engine." icon={Cpu}>
          <p className="panel-description">
            Headset frame generation and the VR overlay use the integrated
            VectorXR engine. No separate VectorXR application is needed.
          </p>
          <span className="badge subtle">VECTORXR · MPL-2.0</span>
          <div className="engine-file-state">
            <span
              className={`live-dot ${runtime.enhancementsAvailable ? "green" : ""}`}
            />
            {runtime.enhancementsAvailable
              ? "Engine files available"
              : "Engine files not confirmed"}
          </div>
        </Panel>
      </div>
    </div>
  );
}

function OverlayPreview({ overlay, framegen, profile, gameName }) {
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
          {overlay.showFramegen && (
            <div className="osd-details">
              <div><span>Generated frames / second</span><strong>-- / --</strong></div>
              <div><span>Generated share of submissions</span><strong>--%</strong></div>
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
              {overlay.showFramegen && (
                <div>
                  <span>Framegen config</span>
                  <strong>
                    {framegen.enabled
                      ? `${framegen.synthesisMode} · ${framegen.frameMultiplier}`
                      : "Off"}
                  </strong>
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
                    <span>Contour / mode</span>
                    <strong>
                      {profile.mask.enabled && profile.mode === "driving"
                        ? "On"
                        : "Off"}{" "}
                      · {profile.mode}
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
  framegen,
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
          framegen={framegen}
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
                { key: "showFramegen", label: "Show frame generation" },
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
              occupied={[
                framegen.toggleShortcut,
                overlay.cycleShortcut,
                profile.centering?.gameShortcut,
              ]}
            />
            <Shortcut
              label="Overlay layout shortcut"
              value={overlay.cycleShortcut}
              onChange={(cycleShortcut) => onChange({ cycleShortcut })}
              occupied={[
                framegen.toggleShortcut,
                overlay.toggleShortcut,
                profile.centering?.gameShortcut,
              ]}
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

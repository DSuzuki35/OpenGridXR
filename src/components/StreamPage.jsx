import {usesFullRenderArea} from "../model.mjs";
import React from "react";
import {Monitor, ScanLine, Eye, ShieldCheck, Expand} from "lucide-react";
import {Button, Panel, Range, Stepper, Toggle, Segmented, Note} from "./Controls.jsx";
import {TrackArt} from "./ContourEditor.jsx";
import {isBrowserPreview} from "../api.mjs";
import {StreamHelmetPreview, StreamHelmetControls} from './StreamHelmet.jsx';
export default function StreamPage({profile,dirty,busy,launch,changeGroup,change,displays,displayError,setExtraSymmetric,changeExtraArea,importSteadyXr,importReport,dismissImportReport,helmetAvailable}) { return (
              <div className="stream-layout">
                <div className="stream-main">
                  {importReport && <Panel title="steadyXR preset imported">
                    <p className="panel-description">Created “{profile.name}” as a new profile with full render area for stream. Save to keep it.</p>
                    <details open><summary>Imported settings</summary><ul>{importReport.imported.map((item,index) => <li key={index}>{item}</li>)}</ul></details>
                    {importReport.warnings.length > 0 && <div className="stream-import-warnings"><strong>Review these differences</strong><ul>{importReport.warnings.map((item,index) => <li key={index}>{item}</li>)}</ul></div>}
                    <Button onClick={dismissImportReport}>Dismiss import summary</Button>
                  </Panel>}
                  <Panel
                    title="Clean output"
                    icon={Monitor}
                    action={
                      <span className="badge subtle">COMPOSITION PREVIEW</span>
                    }
                    className="stream-preview"
                  >
                    <div className="stream-scene" style={{aspectRatio:`${profile.stream.width} / ${profile.stream.height}`}}>
                      <svg
                        viewBox="0 0 800 450"
                        aria-label="Stream composition illustration"
                        preserveAspectRatio="xMidYMid slice"
                      >
                        <TrackArt id="stream-art" />
                        <rect
                          x="50"
                          y="28"
                          width="700"
                          height="394"
                          rx="3"
                          stroke="#c5b5f5"
                          strokeOpacity=".6"
                          strokeWidth="1"
                          fill="none"
                          strokeDasharray="6 5"
                        />
                        <path
                          d="M400 165v120M340 225h120"
                          stroke="#c4b5e9"
                          strokeOpacity=".3"
                          strokeWidth="1"
                        />
                      </svg>
                      {helmetAvailable && <StreamHelmetPreview profile={profile} />}
                      <div className="output-overlay">
                        <span>
                          <span className="live-dot" /> NO LIVE FEED
                        </span>
                        <span>
                          {profile.stream.width} × {profile.stream.height} /{" "}
                          {profile.stream.fps} FPS
                        </span>
                      </div>
                    </div>
                    <div className="output-actions">
                      <div>
                        <strong>Your drive. On the screen you choose.</strong>
                        <p>
                          {dirty
                            ? "Save this profile before opening clean output."
                            : "Capture the clean window in OBS or send it fullscreen to your capture card."}
                        </p>
                      </div>
                      <Button
                        variant="primary"
                        icon={Expand}
                        onClick={launch}
                        disabled={!!busy || dirty}
                        title={
                          dirty
                            ? "Save this profile before opening output"
                            : "Open clean output"
                        }
                      >
                        {busy === "launch" ? "Opening…" : "Open output"}
                      </Button>
                    </div>
                  </Panel>
                  <StreamHelmetControls profile={profile} changeGroup={changeGroup} available={helmetAvailable} />
                  <div className="stream-bottom-grid">
                    <Panel title="Frame & output" icon={ScanLine}>
                      <div className="select-field">
                        <label htmlFor="output-size">Output resolution</label>
                        <select
                          id="output-size"
                          value={`${profile.stream.width}x${profile.stream.height}`}
                          onChange={(event) => {
                            const [width, height] = event.target.value
                              .split("x")
                              .map(Number);
                            changeGroup("stream", { width, height });
                          }}
                        >
                          {![
                            "1280x720",
                            "1920x1080",
                            "2560x1440",
                            "3840x2160",
                          ].includes(
                            `${profile.stream.width}x${profile.stream.height}`,
                          ) && (
                            <option
                              value={`${profile.stream.width}x${profile.stream.height}`}
                            >
                              Custom · {profile.stream.width} ×{" "}
                              {profile.stream.height}
                            </option>
                          )}
                          <option value="1280x720">HD · 1280 × 720</option>
                          <option value="1920x1080">
                            Full HD · 1920 × 1080
                          </option>
                          <option value="2560x1440">QHD · 2560 × 1440</option>
                          <option value="3840x2160">4K · 3840 × 2160</option>
                        </select>
                      </div>
                      <div className="select-field">
                        <label htmlFor="output-fps">Frame rate target</label>
                        <select
                          id="output-fps"
                          value={profile.stream.fps}
                          onChange={(event) =>
                            changeGroup("stream", {
                              fps: Number(event.target.value),
                            })
                          }
                        >
                          {[30, 60, 90, 120].map((fps) => (
                            <option key={fps} value={fps}>
                              {fps} fps
                            </option>
                          ))}
                        </select>
                      </div>
                      <Toggle
                        label="Mirrored borders"
                        bindingAction="stream.mirror"
                        checked={profile.stream.mirror}
                        onChange={(mirror) => changeGroup("stream", { mirror })}
                      />
                      <Range label="Mirror limit" max={50} step={.1} value={profile.stream.mirrorLimit ?? 50} disabled={!profile.stream.mirror}
                        onChange={mirrorLimit => changeGroup('stream', {mirrorLimit})}
                        hint={profile.stream.mirrorLimit === undefined ? 'Legacy unlimited mirroring is preserved until you adjust this control.' : 'Maximum reflected area beyond each image edge. Lower values also limit camera correction.'} />
                      <Range label="Mirror softness" step={.1} value={profile.stream.mirrorSoftness ?? 0} disabled={!profile.stream.mirror}
                        onChange={mirrorSoftness => changeGroup('stream', {mirrorSoftness, mirrorLimit:profile.stream.mirrorLimit ?? 50})}
                        hint="Soften reflected scenery outside the real image." />
                      <Range label="Mirror stretch" step={.1} value={profile.stream.mirrorStretch ?? 0} disabled={!profile.stream.mirror}
                        onChange={mirrorStretch => changeGroup('stream', {mirrorStretch, mirrorLimit:profile.stream.mirrorLimit ?? 50})}
                        hint="Blend mirrored scenery toward stretched edge colors." />
                      <div className="panel-divider" />
                      <div className="select-field">
                        <label htmlFor="output-display">Output display</label>
                        <select
                          id="output-display"
                          value={profile.stream.displayId || "primary"}
                          onChange={(event) =>
                            changeGroup("stream", {
                              displayId: event.target.value,
                            })
                          }
                        >
                          <option value="primary">
                            {isBrowserPreview
                              ? "Primary display · browser preview"
                              : "Primary display · automatic"}
                          </option>
                          {displays
                            .filter((display) => display.id !== "primary")
                            .map((display) => (
                              <option key={display.id} value={display.id}>
                                {display.label}
                                {display.primary ? " · primary" : ""} ·{" "}
                                {display.width} × {display.height}
                              </option>
                            ))}
                          {profile.stream.displayId &&
                            profile.stream.displayId !== "primary" &&
                            !displays.some(
                              (display) =>
                                display.id === profile.stream.displayId,
                            ) && (
                              <option value={profile.stream.displayId}>
                                Previously selected display · unavailable
                              </option>
                            )}
                        </select>
                      </div>
                      <Toggle
                        label="Start fullscreen"
                        bindingAction="stream.fullscreen"
                        description="Borderless output on the selected display"
                        checked={profile.stream.fullscreen !== false}
                        onChange={(fullscreen) =>
                          changeGroup("stream", { fullscreen })
                        }
                      />
                      {displayError && <Note tone="amber">{displayError}</Note>}
                      <Note>
                        For a 2-PC setup, connect your capture card by HDMI and
                        set that output to an extended display in Windows.{" "}
                        <kbd>F11</kbd> toggles fullscreen; <kbd>Esc</kbd>{" "}
                        returns to windowed.
                      </Note>
                    </Panel>
                    <Panel title="Stabilization reserve" icon={ShieldCheck}>
                      <p className="panel-description">
                        Extra image area for smoother framing.
                      </p>
                      <Range label="Global reserve" max={30}
                        value={Math.round(Object.values(profile.stream.reserve).reduce((sum, value) => sum + value, 0) / 4)}
                        hint="Sets all four sides to the same amount. Fine-tune individual sides below."
                        onChange={(value) => changeGroup("stream", { reserve: { left: value, right: value, top: value, bottom: value } })} />
                      <details className="reserve-details">
                        <summary>Fine-tune individual sides{new Set(Object.values(profile.stream.reserve)).size > 1 ? " · Individual values" : " · All sides equal"}</summary>
                        {new Set(Object.values(profile.stream.reserve)).size > 1 && <p className="panel-description">The global control shows the average. Moving it sets all sides equally again.</p>}
                        <div className="crop-fields">
                        {["left", "right", "top", "bottom"].map((edge) => (
                          <Stepper
                            key={edge}
                            label={`Reserve ${edge}`}
                            value={profile.stream.reserve[edge]}
                            max={30}
                            onChange={(value) =>
                              change((next) => {
                                next.stream.reserve[edge] = value;
                              })
                            }
                          />
                        ))}
                      </div>
                      </details>
                    </Panel>
                  </div>
                </div>
                <div className="settings-stack">
                  <Panel title="steadyXR presets">
                    <p className="panel-description">Import a steadyXR INI preset into a new profile for this game. A summary shows converted and unsupported settings.</p>
                    <Button onClick={importSteadyXr} disabled={!!busy}>Import steadyXR preset</Button>
                  </Panel>
                  <Panel title="Camera behavior" icon={Eye}>
                    <div className="preset-label top">SOURCE EYE</div>
                    <Segmented
                      label="Stream source eye"
                      value={profile.stream.eye}
                      onChange={(eye) => changeGroup("stream", { eye })}
                      options={[
                        { value: 0, label: "Left eye" },
                        { value: 1, label: "Right eye" },
                      ]}
                    />
                    <div className="panel-divider" />
                    <Range
                      label="Motion smoothing"
                      value={profile.stream.smoothing}
                      onChange={(smoothing) =>
                        changeGroup("stream", { smoothing })
                      }
                    />
                    <Range
                      label="Horizon lock"
                      value={profile.stream.horizonLock}
                      onChange={(horizonLock) =>
                        changeGroup("stream", { horizonLock })
                      }
                    />
                    <Toggle label="Adaptive roll leveling" checked={profile.stream.adaptiveHorizon === true}
                      onChange={adaptiveHorizon => changeGroup('stream', {adaptiveHorizon})}
                      description="Level small tilts more strongly while allowing deliberate head lean. Uses the Horizon lock strength above." />
                    <Range label="Pitch stabilization" step={.1} value={profile.stream.pitchLock ?? 0}
                      onChange={pitchLock => changeGroup('stream', {pitchLock})}
                      hint="Reduce small up/down movements around the starting view; larger looks remain possible. Press R in the output window to reset the reference." />
                    <Range
                      label="Horizontal framing"
                      value={profile.stream.offsetX}
                      min={-100}
                      max={100}
                      onChange={(offsetX) => changeGroup("stream", { offsetX })}
                    />
                    <Range
                      label="Vertical framing"
                      value={profile.stream.offsetY}
                      min={-100}
                      max={100}
                      onChange={(offsetY) => changeGroup("stream", { offsetY })}
                    />
                    <div className="panel-divider" />
                    <Toggle label="Keep full render area for stream" checked={usesFullRenderArea(profile)}
                      onChange={preserveFullFrame => changeGroup("stream", {preserveFullFrame})}
                      description="Keep the headset render area intact and apply configured extra area. Crop frames only the stream output; the contour is inactive. Save and restart the XR session to apply." />
                    <Toggle
                      label="Symmetric extra area"
                      bindingAction="stream.extraSymmetric"
                      description={
                        profile.stream.extraSymmetric
                          ? "Left and right move together"
                          : "Link both sides using the larger value"
                      }
                      checked={profile.stream.extraSymmetric === true}
                      onChange={setExtraSymmetric}
                    />
                    <Range
                      label="Extra area · left"
                      value={profile.stream.extraLeft}
                      max={30}
                      onChange={(value) => changeExtraArea("extraLeft", value)}
                    />
                    <Range
                      label="Extra area · right"
                      value={profile.stream.extraRight}
                      max={30}
                      onChange={(value) => changeExtraArea("extraRight", value)}
                    />
                  </Panel>
                  <Note
                    tone={usesFullRenderArea(profile) ? "violet" : "amber"}
                  >
                    {usesFullRenderArea(profile)
                      ? "The full render area and configured extra area supply the stream. Crop frames only the output; the contour is inactive. Save and restart the XR session to apply."
                      : "Crop and contour reduce the game view where supported. Extra stream area is inactive. Save and restart the XR session to apply."}
                  </Note>
                </div>
              </div>

);}

import React, { useEffect, useId, useState } from "react";
import {
  ArrowRight,
  Check,
  Eye,
  FileCheck2,
  FileSearch,
  FolderOpen,
  Layers,
  RefreshCw,
  RotateCcw,
  ScanLine,
  ShieldCheck,
} from "lucide-react";
import { api, isBrowserPreview } from "../api.mjs";
import { nativeFoveationDefaults } from "../enhancements.mjs";
import { Button, Note, Panel, Range } from "./Controls.jsx";

const KEYS = {
  FoveatedOuterPctRes: "Peripheral resolution",
  FoveatedInsetWidthPct: "Focus width",
  ResolutionScalePct: "Game supersampling",
};

function FocusDiagram({ values }) {
  const id = useId().replace(/:/g, "");
  const outerArea = (values.outerResolution / 100) ** 2 * 100;
  const focusArea = (values.focusWidth / 100) ** 2 * 100;
  return (
    <Panel
      title="Two eyes. A focused center."
      icon={Eye}
      action={<span className="badge subtle">GEOMETRY ILLUSTRATION</span>}
      className="native-focus-preview"
    >
      <svg
        viewBox="0 0 660 250"
        aria-label="Illustrative peripheral and focus views"
      >
        <defs>
          <pattern
            id={`${id}-outer`}
            width={24 - values.outerResolution * 0.25}
            height={24 - values.outerResolution * 0.25}
            patternUnits="userSpaceOnUse"
          >
            <path
              d="M20 0H0V20"
              fill="none"
              stroke="#9777ba"
              strokeOpacity=".17"
            />
          </pattern>
          <pattern
            id={`${id}-focus`}
            width="7"
            height="7"
            patternUnits="userSpaceOnUse"
          >
            <path
              d="M7 0H0V7"
              fill="none"
              stroke="#c6a8e8"
              strokeOpacity=".22"
            />
          </pattern>
        </defs>
        {[0, 1].map((eye) => {
          const x = eye ? 355 : 25;
          const size = (280 * values.focusWidth) / 100;
          const height = (155 * values.focusWidth) / 100;
          return (
            <g key={eye}>
              <text
                x={x + 140}
                y="28"
                textAnchor="middle"
                fontSize="9"
                letterSpacing="2"
                fill="#8b6b9f"
              >
                {eye ? "RIGHT EYE" : "LEFT EYE"}
              </text>
              <rect
                x={x}
                y="49"
                width="280"
                height="155"
                rx="25"
                fill={`url(#${id}-outer)`}
                stroke="#655075"
              />
              <path
                d={`M${x + 140} 49V204M${x} 126.5H${x + 280}`}
                stroke="#705187"
                strokeOpacity=".35"
                strokeDasharray="4 5"
              />
              <rect
                x={x + 140 - size / 2}
                y={126.5 - height / 2}
                width={size}
                height={height}
                rx="7"
                fill="#7954b526"
                stroke="#bc96ed"
                strokeWidth="1.5"
              />
              <rect
                x={x + 140 - size / 2}
                y={126.5 - height / 2}
                width={size}
                height={height}
                rx="7"
                fill={`url(#${id}-focus)`}
              />
              <path d={`M${x + 135} 126.5h10m-5-5v10`} stroke="#c5a2f0" />
              <text
                x={x + 140}
                y="229"
                textAnchor="middle"
                fontSize="8"
                letterSpacing="1"
                fill="#aa85c0"
              >
                PERIPHERY {values.outerResolution}% · FOCUS WIDTH{" "}
                {values.focusWidth}%
              </text>
            </g>
          );
        })}
      </svg>
      <div className="native-area-metrics">
        <div>
          <span>OUTER PLANE</span>
          <strong>
            {outerArea.toFixed(1)}
            <small>%</small>
          </strong>
        </div>
        <span>+</span>
        <div>
          <span>FOCUS PLANE</span>
          <strong>
            {focusArea.toFixed(1)}
            <small>%</small>
          </strong>
        </div>
        <span>=</span>
        <div>
          <span>ILLUSTRATIVE PIXEL AREA</span>
          <strong>
            {(outerArea + focusArea).toFixed(1)}
            <small>%</small>
          </strong>
        </div>
      </div>
      <p className="native-area-note">
        Outer resolution² + focus width², before supersampling. A simplified
        pixel-area illustration, not measured rendering cost or FPS.
      </p>
    </Panel>
  );
}

export default function NativeFoveationPanel({
  profile,
  onChange,
  onNavigate,
  run,
  busy,
}) {
  const values = { ...nativeFoveationDefaults(), ...profile.nativeFoveation };
  const [inspection, setInspection] = useState(null);
  const [isInspecting, setIsInspecting] = useState(false);
  const [readError, setReadError] = useState("");
  const [preview, setPreview] = useState(null);
  const [backupId, setBackupId] = useState("");
  const valueIdentity = `${profile.id}:${profile.game}:${values.outerResolution}:${values.focusWidth}:${values.resolutionScale}`;
  const disabled = !!busy || isInspecting || isBrowserPreview;
  const combinedScale =
    ((values.resolutionScale / 100) * profile.view.renderScale) / 100;

  useEffect(() => {
    setPreview(null);
  }, [valueIdentity]);
  useEffect(() => {
    if (preview)
      window.document
        .getElementById("native-change-review")
        ?.scrollIntoView({ block: "nearest" });
  }, [preview]);
  useEffect(() => {
    let current = true;
    setPreview(null);
    setInspection(null);
    setReadError("");
    if (profile.game !== "iracing" || isBrowserPreview) return;
    if (typeof api.nativeSettings !== "function") {
      setReadError(
        "Native game-settings inspection is not available in this build.",
      );
      return;
    }
    setIsInspecting(true);
    api
      .nativeSettings()
      .then((result) => {
        if (current) setInspection(result);
      })
      .catch((error) => {
        if (current) setReadError(error.message);
      })
      .finally(() => {
        if (current) setIsInspecting(false);
      });
    return () => {
      current = false;
    };
  }, [profile.game]);

  const accept = (result) => {
    if (!result) return;
    setInspection(result);
    setPreview(null);
    setReadError("");
    if (!result.backups?.some((item) => item.id === backupId)) setBackupId("");
  };
  const edit = (key, value) => {
    setPreview(null);
    onChange({ [key]: Math.round(value) });
  };
  const refresh = () =>
    run("native-read", async () => accept(await api.nativeSettings()));
  const choose = () =>
    run("native-choose", async () => accept(await api.chooseNativeSettings()));
  const review = () =>
    run("native-review", async () => {
      setPreview(null);
      const result = await api.previewNativeSettings(
        values,
        inspection.revision,
      );
      setPreview(result);
    });
  const apply = () =>
    run(
      "native-apply",
      async () => {
        const token = preview.token;
        setPreview(null);
        const result = await api.applyNativeSettings(token);
        accept(result);
        return result;
      },
      (result) =>
        result.message ||
        "iRacing settings applied. Restart the sim to use them.",
    );
  const restore = () =>
    run(
      "native-restore",
      async () => {
        setPreview(null);
        const result = await api.restoreNativeSettings(
          backupId,
          inspection.revision,
        );
        accept(result);
        return result;
      },
      (result) =>
        result.message || "The complete renderer settings file was restored.",
    );
  const shortcuts = (
    <div className="native-shortcuts">
      <Button
        icon={ScanLine}
        onClick={() => onNavigate("view", "view-boundaries")}
      >
        Supersampling / render scale <ArrowRight size={13} />
      </Button>
      <Button
        icon={Eye}
        onClick={() => onNavigate("view", "headset-sharpening")}
      >
        Headset sharpening <ArrowRight size={13} />
      </Button>
    </div>
  );

  if (profile.game !== "iracing")
    return (
      <div className="native-unsupported">
        <Panel
          title="Le Mans Ultimate"
          icon={Eye}
          action={
            <span className="badge subtle">NO VERIFIED NATIVE ADAPTER</span>
          }
        >
          <h3>Keep control of the features we can verify.</h3>
          <p>
            There is no verified native foveation settings adapter for Le Mans
            Ultimate in this build. This page does not inject generic XR_VARJO
            quad views or write unsupported game settings.
          </p>
          {shortcuts}
        </Panel>
        <Note tone="violet">
          Your existing supersampling, crop and sharpening settings remain
          available in View & crop.
        </Note>
      </div>
    );

  return (
    <div className="enhancement-layout native-foveation-layout">
      <div className="enhancement-main">
        <FocusDiagram values={values} />
        <Panel
          title="iRacing native Quad View"
          icon={Layers}
          action={<span className="badge">NVIDIA MVP</span>}
        >
          <p className="panel-description">
            iRacing’s own foveated renderer uses NVIDIA Multi-View Projection.
            These controls edit its documented game settings; they do not add a
            generic XR_VARJO quad-view layer.
          </p>
          <ol className="native-setup-steps">
            <li>
              <span>01</span>
              <div>
                <strong>Select Foveated in iRacing Graphics → VR mode.</strong>
                <p>Use the game’s native option, then restart the sim.</p>
              </div>
            </li>
            <li>
              <span>02</span>
              <div>
                <strong>Review the peripheral and focus settings below.</strong>
                <p>
                  OpenGrid saves these preferences to your profile. Applying
                  them to the game is a separate action.
                </p>
              </div>
            </li>
            <li>
              <span>03</span>
              <div>
                <strong>Enable eye tracking in-game if you want it.</strong>
                <p>
                  Use AllowEyeTracking and ShowEyeTracking where supported, and
                  grant the headset/runtime permission. This adapter does not
                  change eye-tracking keys or VR mode.
                </p>
              </div>
            </li>
          </ol>
          <Note>
            Headset and runtime support must be checked in the sim. No special
            optics profile is required by this settings adapter.
          </Note>
        </Panel>
        <Panel
          title="Game renderer settings file"
          icon={FileSearch}
          action={
            <Button
              icon={RefreshCw}
              variant="ghost"
              onClick={refresh}
              disabled={disabled}
            >
              Refresh
            </Button>
          }
        >
          <div className="native-file-path">
            <FileCheck2 size={19} />
            <div>
              <strong>
                {isInspecting
                  ? "Looking for the generated renderer file…"
                  : inspection?.available
                    ? "Renderer file available"
                    : "Renderer file not available"}
              </strong>
              <code>{inspection?.path || "rendererDX11OpenXR.ini"}</code>
            </div>
            <Button icon={FolderOpen} onClick={choose} disabled={disabled}>
              Choose file
            </Button>
          </div>
          {inspection?.message && (
            <p className="native-file-message">{inspection.message}</p>
          )}
          {(readError || isBrowserPreview) && (
            <Note tone="amber">
              {isBrowserPreview
                ? "Use the Windows desktop app to inspect and edit the game’s renderer file."
                : readError}
            </Note>
          )}
          {!inspection?.available && (
            <Note>
              Start iRacing in OpenXR once, exit the sim, then select its
              generated rendererDX11OpenXR.ini. OpenGrid does not create a
              replacement game file.
            </Note>
          )}
          {inspection?.available && (
            <dl className="detail-list native-current-values">
              <div>
                <dt>Peripheral resolution in file</dt>
                <dd>
                  {inspection.values?.outerResolution ?? "Not set"}
                  {inspection.values?.outerResolution != null && "%"}
                </dd>
              </div>
              <div>
                <dt>Focus width in file</dt>
                <dd>
                  {inspection.values?.focusWidth ?? "Not set"}
                  {inspection.values?.focusWidth != null && "%"}
                </dd>
              </div>
              <div>
                <dt>Game supersampling in file</dt>
                <dd>
                  {inspection.values?.resolutionScale ?? "Not set"}
                  {inspection.values?.resolutionScale != null && "%"}
                </dd>
              </div>
            </dl>
          )}
          <div className="native-review-action">
            <Button
              variant="primary"
              icon={FileCheck2}
              onClick={review}
              disabled={disabled || !inspection?.available}
            >
              Review game changes
            </Button>
            <span>Close iRacing before applying changes.</span>
          </div>
        </Panel>
        {preview && (
          <Panel
            title="Review the exact changes"
            id="native-change-review"
            icon={FileCheck2}
            action={<span className="badge subtle">NOT APPLIED YET</span>}
            className="native-review-panel"
          >
            <code className="native-review-path">{preview.path}</code>
            {preview.changes.length ? (
              <table className="native-change-table">
                <thead>
                  <tr>
                    <th>Setting</th>
                    <th>Current file</th>
                    <th>Proposed</th>
                  </tr>
                </thead>
                <tbody>
                  {preview.changes.map((change) => (
                    <tr key={change.key}>
                      <td>
                        <strong>{KEYS[change.key] || change.key}</strong>
                        <code>{change.key}</code>
                      </td>
                      <td>
                        {change.before == null
                          ? "Not set"
                          : `${change.before}%`}
                      </td>
                      <td>{change.after}%</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <p className="panel-description">
                The file already matches these profile values.
              </p>
            )}
            <Note>
              {preview.message ||
                "The existing file is backed up exactly before these changes are written. Other settings are preserved."}
            </Note>
            <div className="native-review-action">
              <Button
                variant="primary"
                icon={Check}
                onClick={apply}
                disabled={disabled || !preview.changes.length}
              >
                Apply to iRacing
              </Button>
              <Button
                variant="ghost"
                onClick={() => setPreview(null)}
                disabled={!!busy}
              >
                Cancel review
              </Button>
            </div>
          </Panel>
        )}
      </div>
      <div className="settings-stack">
        <Panel title="Native foveation preferences" icon={Eye}>
          <Range
            label="Peripheral resolution"
            min={25}
            max={50}
            value={values.outerResolution}
            onChange={(value) => edit("outerResolution", value)}
          />
          <Range
            label="Focus width"
            min={25}
            max={50}
            value={values.focusWidth}
            onChange={(value) => edit("focusWidth", value)}
          />
          <div className="panel-divider" />
          <Range
            label="Game supersampling"
            min={50}
            max={200}
            value={values.resolutionScale}
            onChange={(value) => edit("resolutionScale", value)}
            hint="iRacing’s ResolutionScalePct setting, separate from OpenGrid’s layer render scale."
          />
          <div className="native-combined-scale">
            <span>GAME × OPENGRID LAYER</span>
            <strong>
              {combinedScale.toFixed(2)}× <small>per axis</small>
            </strong>
            <p>
              {Math.round(combinedScale ** 2 * 100)}% nominal pixel area before
              foveation.
            </p>
          </div>
          <Note>
            Save profile stores these preferences in OpenGrid. Use Review game
            changes to inspect the separate game-file update.
          </Note>
        </Panel>
        <Panel title="Exact file backups" icon={ShieldCheck}>
          <p className="panel-description">
            Restoring a backup replaces the complete renderer file, including
            unrelated iRacing preferences.
          </p>
          <div className="select-field">
            <label htmlFor="native-backup-select">Backup to restore</label>
            <select
              id="native-backup-select"
              value={backupId}
              onChange={(event) => setBackupId(event.target.value)}
              disabled={disabled || !inspection?.backups?.length}
            >
              <option value="">Select a saved backup</option>
              {inspection?.backups?.map((backup) => (
                <option key={backup.id} value={backup.id}>
                  {backup.createdAt
                    ? new Date(backup.createdAt).toLocaleString()
                    : backup.id}
                </option>
              ))}
            </select>
          </div>
          {!inspection?.backups?.length && (
            <p className="selection-hint">
              An exact backup is created when game changes are applied.
            </p>
          )}
          <div className="vertical-actions native-backup-actions">
            <Button
              icon={RotateCcw}
              onClick={restore}
              disabled={disabled || !backupId}
            >
              Restore full file
            </Button>
            <Button
              icon={FolderOpen}
              onClick={() =>
                run("native-folder", () => api.openNativeBackups())
              }
              disabled={disabled}
            >
              Open backup folder
            </Button>
          </div>
        </Panel>
        <Note tone="violet">
          Native game settings and OpenGrid’s render scale combine. Use one
          setting at a time when checking image quality in your headset.
        </Note>
        {shortcuts}
      </div>
    </div>
  );
}

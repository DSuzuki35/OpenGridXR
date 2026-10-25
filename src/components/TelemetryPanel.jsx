import React, { useEffect, useState } from "react";
import {
  Activity,
  Clock3,
  Flag,
  Fuel,
  Gauge,
  Radio,
  Settings2,
} from "lucide-react";
import { api } from "../api.mjs";
import { Note, Panel, Range, Toggle } from "./Controls.jsx";

const FLAGS = ["yellow", "blue", "green", "white", "checkered", "red", "black"];
const PROTOCOLS = {
  "iracing-sdk": "iRacing SDK shared memory",
  "lmu-native": "LMU_Data · built-in shared memory",
};
const number = (value) =>
  typeof value === "number" && Number.isFinite(value) ? value : null;
const positive = (value) => {
  const n = number(value);
  return n !== null && n >= 0 ? n : null;
};
const display = (value, digits = 0) =>
  positive(value) === null ? "--" : value.toFixed(digits);
function lapTime(value) {
  if (positive(value) === null) return "--:--.---";
  const ms = Math.round(value * 1000);
  const minutes = Math.floor(ms / 60000);
  return `${minutes}:${String(Math.floor(ms / 1000) % 60).padStart(2, "0")}.${String(ms % 1000).padStart(3, "0")}`;
}

function Pedal({ name, value, live }) {
  const raw = live ? number(value) : null;
  const percent =
    raw === null ? null : Math.round(Math.max(0, Math.min(1, raw)) * 100);
  return (
    <div className={`telemetry-pedal ${name.toLowerCase()}`}>
      <div>
        <span>{name}</span>
        <strong>
          {percent === null ? "--" : percent}
          <small>%</small>
        </strong>
      </div>
      <div className="pedal-track">
        <span style={{ width: `${percent ?? 0}%` }} />
      </div>
    </div>
  );
}

export default function TelemetryPanel({ profile, onChange }) {
  const preferences = { enabled: false, updateHz: 20, ...profile.telemetry };
  const [snapshot, setSnapshot] = useState(null);
  const [error, setError] = useState("");
  const [paused, setPaused] = useState(() => window.document.hidden);

  useEffect(() => {
    let disposed = false;
    let timer;
    let expiry;
    let inFlight = false;
    const game = profile.game;
    setSnapshot(null);
    setError("");
    setPaused(window.document.hidden);
    const poll = async () => {
      if (disposed || window.document.hidden || inFlight) return;
      inFlight = true;
      try {
        if (typeof api.telemetryStatus !== "function")
          throw new Error("Telemetry status is not available in this build.");
        const result = await api.telemetryStatus(game);
        if (!disposed && !window.document.hidden) {
          setSnapshot({ game, value: result });
          setError("");
          clearTimeout(expiry);
          if (result.record?.status === "live")
            expiry = setTimeout(() => {
              setSnapshot((current) =>
                current?.value === result
                  ? {
                      game,
                      value: {
                        ...result,
                        record: {
                          ...result.record,
                          status: "stale",
                          stale: true,
                          ageMs: (result.record.ageMs ?? 0) + 1500,
                          reason: "No fresh telemetry status has arrived.",
                        },
                      },
                    }
                  : current,
              );
            }, 1500);
        }
      } catch (failure) {
        if (!disposed) {
          setSnapshot(null);
          setError(failure.message || "Could not read telemetry status.");
        }
      } finally {
        inFlight = false;
        if (!disposed && !window.document.hidden) timer = setTimeout(poll, 200);
      }
    };
    const visibility = () => {
      clearTimeout(timer);
      clearTimeout(expiry);
      setPaused(window.document.hidden);
      setSnapshot(null);
      if (!window.document.hidden && !inFlight) timer = setTimeout(poll, 0);
    };
    window.document.addEventListener("visibilitychange", visibility);
    timer = setTimeout(poll, 0);
    return () => {
      disposed = true;
      clearTimeout(timer);
      clearTimeout(expiry);
      window.document.removeEventListener("visibilitychange", visibility);
    };
  }, [profile.game]);

  const status = snapshot?.game === profile.game ? snapshot.value : null;
  const record = status?.record;
  const live =
    !paused &&
    !error &&
    status?.available === true &&
    status?.enabled === true &&
    status?.running === true &&
    record?.schemaVersion === 1 &&
    record?.game === profile.game &&
    record?.status === "live" &&
    record?.connected === true &&
    record?.stale !== true;
  const stale = record?.status === "stale" || record?.stale === true;
  const data = live ? record.data || {} : {};
  const gear = number(data.gear);
  const activeFlags = live
    ? FLAGS.filter((flag) => data.flags?.[flag] === true)
    : [];
  const flagsKnown =
    live && FLAGS.some((flag) => typeof data.flags?.[flag] === "boolean");
  const statusLabel = paused
    ? "Paused while hidden"
    : error
      ? "Status unavailable"
      : !status
        ? "Waiting for telemetry status"
        : !status.enabled
          ? "Telemetry disabled"
          : !status.available
            ? "Telemetry unavailable"
            : record?.status === "unsupported"
              ? "Unsupported source"
              : record?.status === "invalid"
                ? "Invalid sample"
                : stale
                  ? "Stale data"
                  : live
                    ? record.testData
                      ? "Test fixture data"
                      : "Live telemetry"
                    : record?.connected === false ||
                        record?.status === "disconnected"
                      ? "Game disconnected"
                      : !status.running
                        ? "Collector stopped"
                        : "Waiting for the game";
  const sampleTime = positive(record?.timestampMs);

  return (
    <div className="enhancement-layout telemetry-layout">
      <div className="enhancement-main">
        <div
          className={`telemetry-status-banner ${live ? "live" : stale ? "stale" : "offline"}`}
          role="status"
        >
          <Radio size={18} />
          <div>
            <strong>{statusLabel}</strong>
            <p>
              {paused
                ? "Status polling resumes when this window is visible."
                : error ||
                  (stale
                    ? record?.reason ||
                      "The last sample is stale. Waiting for fresh data."
                    : status?.message) ||
                  "Reading the selected game’s source status…"}
            </p>
          </div>
          <span
            className={`live-dot ${live ? "green" : stale ? "amber" : ""}`}
          />
        </div>
        <Panel
          title="Vehicle signals"
          icon={Gauge}
          action={
            <span className={`badge ${live ? "good" : "subtle"}`}>
              {live
                ? record.testData
                  ? "TEST FIXTURE"
                  : "LIVE SOURCE"
                : "NO LIVE VALUES"}
            </span>
          }
          className={`vehicle-signals ${live ? "is-live" : "is-unavailable"}`}
        >
          <div className="telemetry-primary">
            <div className="telemetry-speed">
              <span>SPEED</span>
              <strong data-testid="telemetry-speed">
                {positive(data.speedMps) === null
                  ? "--"
                  : (data.speedMps * 3.6).toFixed(0)}
                <small>km/h</small>
              </strong>
            </div>
            <div className="telemetry-gear">
              <span>GEAR</span>
              <strong data-testid="telemetry-gear">
                {gear === null
                  ? "--"
                  : gear === -1
                    ? "R"
                    : gear === 0
                      ? "N"
                      : gear}
              </strong>
            </div>
            <div className="telemetry-rpm">
              <span>ENGINE SPEED</span>
              <strong data-testid="telemetry-rpm">
                {display(data.rpm)}
                <small>rpm</small>
              </strong>
            </div>
          </div>
          <div className="telemetry-pedals">
            <Pedal name="Throttle" value={data.throttle} live={live} />
            <Pedal name="Brake" value={data.brake} live={live} />
            <Pedal name="Clutch" value={data.clutch} live={live} />
          </div>
          <div className="telemetry-vehicle-footer">
            <span>
              {live && typeof data.inPits === "boolean"
                ? data.inPits
                  ? "IN PIT LANE"
                  : "ON TRACK"
                : "TRACK POSITION UNAVAILABLE"}
            </span>
            <span>
              {live
                ? "Read-only game data"
                : "Waiting for a fresh connected sample"}
            </span>
          </div>
        </Panel>
        <div className="enhancement-pair">
          <Panel title="Lap timing" icon={Clock3}>
            <div className="telemetry-current-lap">
              <span>CURRENT LAP</span>
              <strong data-testid="telemetry-lap-time">
                {lapTime(data.lapTimeSeconds)}
              </strong>
            </div>
            <dl className="detail-list">
              <div>
                <dt>Last lap</dt>
                <dd>{lapTime(data.lastLapSeconds)}</dd>
              </div>
              <div>
                <dt>Best lap</dt>
                <dd>{lapTime(data.bestLapSeconds)}</dd>
              </div>
              <div>
                <dt>Lap number</dt>
                <dd>{display(data.lap)}</dd>
              </div>
              <div>
                <dt>Laps completed</dt>
                <dd>{display(data.lapsCompleted)}</dd>
              </div>
            </dl>
          </Panel>
          <Panel title="Fuel & race flags" icon={Fuel}>
            <div className="telemetry-fuel">
              <span>FUEL REMAINING</span>
              <strong>
                {display(data.fuelLiters, 1)}
                <small>L</small>
              </strong>
            </div>
            <div className="telemetry-flag-heading">
              <Flag size={13} />
              <span>FLAGS REPORTED BY THE GAME</span>
            </div>
            <div className="telemetry-flags">
              {activeFlags.length ? (
                activeFlags.map((flag) => (
                  <span key={flag} className={`race-flag ${flag}`}>
                    <Flag size={11} />
                    {flag}
                  </span>
                ))
              ) : (
                <span className="telemetry-empty-flags">
                  {flagsKnown
                    ? "No active race flag reported"
                    : "Flag status unavailable"}
                </span>
              )}
            </div>
          </Panel>
        </div>
        <Note>
          Unavailable channels stay blank. Stale or disconnected samples never
          remain displayed as live vehicle values.
        </Note>
      </div>
      <div className="settings-stack">
        <Panel title="Telemetry collection" icon={Settings2}>
          <Toggle
            label="Enable telemetry"
            bindingAction="telemetry.enabled"
            checked={preferences.enabled}
            onChange={(enabled) => onChange({ enabled })}
            description="Save this setting with your game profile"
          />
          <div className="panel-divider" />
          <Range
            label="Source update rate"
            min={1}
            max={30}
            unit="Hz"
            value={preferences.updateHz}
            onChange={(updateHz) =>
              onChange({ updateHz: Math.round(updateHz) })
            }
            hint="A lower rate reduces collection work. 20 Hz is the default."
          />
          <Note>
            Enable telemetry and save the profile to request collection. This
            screen reads signals only; it sends no driving controls to the game.
          </Note>
        </Panel>
        <Panel title="Source connection" icon={Activity}>
          <dl className="detail-list telemetry-source-details">
            <div>
              <dt>Integration</dt>
              <dd>
                {profile.game === "iracing"
                  ? "iRacing SDK"
                  : "Built-in LMU_Data"}
              </dd>
            </div>
            <div>
              <dt>Record source</dt>
              <dd>{PROTOCOLS[record?.source] || "--"}</dd>
            </div>
            <div>
              <dt>Collector</dt>
              <dd>
                {status ? (status.running ? "Running" : "Stopped") : "--"}
              </dd>
            </div>
            <div>
              <dt>Saved sample target</dt>
              <dd>
                {positive(status?.requestedHz) === null
                  ? "--"
                  : `${status.requestedHz} Hz`}
              </dd>
            </div>
            <div>
              <dt>Sample age</dt>
              <dd>
                {positive(record?.ageMs) === null
                  ? "--"
                  : `${Math.round(record.ageMs)} ms`}
              </dd>
            </div>
            <div>
              <dt>Last sample</dt>
              <dd>
                {sampleTime === null
                  ? "--"
                  : new Date(sampleTime).toLocaleTimeString()}
              </dd>
            </div>
            <div>
              <dt>Sequence</dt>
              <dd>{display(record?.sequence)}</dd>
            </div>
          </dl>
          <p className="panel-description">
            {profile.game === "iracing"
              ? "Reads the simulator’s shared-memory SDK when iRacing provides it."
              : "Uses the current game’s built-in LMU_Data shared memory. No additional telemetry plugin is required."}
          </p>
        </Panel>
        <Note tone="violet">
          The dashboard requests status up to 5 times a second while visible.
          Polling pauses when hidden and stops when you leave this page.
        </Note>
      </div>
    </div>
  );
}

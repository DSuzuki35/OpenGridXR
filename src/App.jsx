import React, { lazy, Suspense, useEffect, useMemo, useRef, useState } from "react";
import {
  Activity,
  Aperture,
  ArrowRight,
  ArrowUpRight,
  Box,
  Check,
  CheckCircle2,
  ChevronDown,
  CircleHelp,
  Copy,
  Download,
  Expand,
  Eye,
  Flag,
  FolderHeart,
  FolderOpen,
  Gauge,
  Gamepad2,
  Grid2X2,
  Info,
  Layers,
  Link2,
  Monitor,
  Plus,
  Radio,
  RefreshCw,
  RotateCcw,
  Save,
  ScanLine,
  Settings2,
  ShieldCheck,
  SlidersHorizontal,
  Trash2,
  Upload,
  X,
} from "lucide-react";
import { api, isBrowserPreview } from "./api.mjs";
import {
  createDocument,
  createProfile,
  contourPreset,
  effectiveMask,
  usesFullRenderArea,
  pixelBudget,
  validateDocument,
} from "./model.mjs";
import {
  Button,
  Note,
  Panel,
  Range,
  Segmented,
  Stepper,
  Toggle,
} from "./components/Controls.jsx";
import ContourEditor, { TrackArt } from "./components/ContourEditor.jsx";
const QuadViewPage = lazy(() => import('./components/QuadViewPage.jsx'));
const LayersPage = lazy(() => import('./components/LayersPage.jsx'));
const TelemetryPanel = lazy(() => import("./components/TelemetryPanel.jsx"));
const BenchmarkPanel = lazy(() => import("./components/BenchmarkPanel.jsx"));
const SpatialOverlaysPanel = lazy(() => import("./components/SpatialOverlaysPanel.jsx"));
import UpdatesPanel from "./components/UpdatesPanel.jsx";
import SupportPanel from "./components/SupportPanel.jsx";
import packageMetadata from "../package.json";
const HelmetEditor = lazy(() => import("./components/HelmetEditor.jsx"));
const ColorCorrectionPanel = lazy(() => import('./components/ColorCorrectionPanel.jsx'));
const InputBindingsProvider = lazy(() => import("./components/InputBindings.jsx").then(m => ({default:m.InputBindingsProvider})));
const InputBindingsPage = lazy(() => import("./components/InputBindings.jsx").then(m => ({default:m.InputBindingsPage})));
import { toggleProfileAction, pruneSpatialBindings } from "./input-bindings.mjs";
import { spatialDefaults, centeringDefaults } from "./spatial-overlays.mjs";
const FrameGenerationPage = lazy(() => import("./components/EnhancementPanels.jsx").then(m => ({default:m.FrameGenerationPage})));
const OverlayPage = lazy(() => import("./components/EnhancementPanels.jsx").then(m => ({default:m.OverlayPage})));
const StreamPage = lazy(() => import("./components/StreamPage.jsx"));
import ModulesPanel from "./components/ModulesPanel.jsx";
import { moduleDefaults } from "./modules.mjs";
import {
  framegenDefaults,
  overlayDefaults,
  sharpeningDefaults,
} from "./enhancements.mjs";

const NAV = [
  { id: "session", label: "Session", icon: Flag },
  { id: "telemetry", label: "Telemetry", icon: Activity },
  { id: "benchmark", label: "Lap benchmark", icon: Gauge },
  { id: "view", label: "View & crop", icon: ScanLine },
  { id: 'lmuRendering', label: 'LMU quad views & VRS', icon: Eye },
  { id: "helmet", label: "Helmet", icon: Aperture },
  { id: 'colorCorrection', label: 'Color correction', icon: SlidersHorizontal },
  { id: "stream", label: "Stream", icon: Radio },
  { id: "framegen", label: "Frame generation", icon: Layers },
  { id: "overlay", label: "Performance HUD", icon: Gauge },
  { id: "spatial", label: "Spatial overlays", icon: Monitor },
  { id: "bindings", label: "Controller bindings", icon: Gamepad2 },
  { id: "profiles", label: "Profiles", icon: FolderHeart },
  { id: "setup", label: "Setup", icon: SlidersHorizontal },
  { id: "layers", label: "OpenXR layers", icon: Layers },
];
const GAME_NAMES = { iracing: "iRacing", lmu: "Le Mans Ultimate" };
const BUSY_LABELS = {
  save: "Saving your profile…",
  restore: "Restoring your profiles…",
  install: "Installing the runtime layer…",
  remove: "Removing the runtime layer…",
  launch: "Opening clean output…",
  "lmu-offline": "Starting an offline LMU OpenXR test…",
  import: "Importing a profile…",
  "import-steadyxr": "Importing a steadyXR preset…",
  export: "Exporting your profile…",
  refresh: "Checking the runtime…",
  folder: "Opening the data folder…",
  "overlay-windows": "Finding current desktop windows…",
  "overlay-select": "Selecting this window for capture…",
  "overlay-refresh": "Reading overlay host status…",
  "overlay-recenter": "Centering play-space overlay anchors…",
  "center-game": "Bring the selected sim to the foreground now…",
};
const PAGE = {
  colorCorrection: ['HEADSET COLORS', 'Adjust the light. Keep the view.', 'Independent VR color presets; your stream stays unchanged.'],
  layers: ['OPENXR LAYERS', 'Your layers. Your control.', 'Inspect registrations, manage availability and restore your changes.'],
  lmuRendering: ['LMU RENDERING', 'A shared view of the track.', 'Quad views and stereo VRS with fixed focus sizing and verified game hooks.'],
  benchmark: ["MEASURE YOUR SESSION", "Every lap tells a story.", "Compare frame timing, generated frames, traffic and weather."],
  session: [
    "YOUR NEXT SESSION",
    "Everything in its place.",
    "A clear view of your profile, runtime and next race.",
  ],
  view: [
    "SEE WHAT MATTERS",
    "Your view. Your advantage.",
    "Keep the road. Shape everything around it.",
  ],
  telemetry: [
    "STRAIGHT FROM THE SIM",
    "The numbers behind the lap.",
    "Read-only vehicle signals with clear source and freshness status.",
  ],
  helmet: [
    "FIND YOUR FIT",
    "Built around your perspective.",
    "Shape your view and design your own visor strip.",
  ],
  stream: [
    "SHARE THE DRIVE",
    "A better seat for your audience.",
    "Frame a clean, stabilized view without compromising your drive.",
  ],
  profiles: [
    "SET IT ONCE",
    "Every car. Every preference.",
    "Keep your favorite setups close and your settings portable.",
  ],
  framegen: [
    "BETWEEN THE FRAMES",
    "More motion. Inside your headset.",
    "Configure experimental headset frame generation with the included engine.",
  ],
  overlay: [
    "YOUR SESSION AT A GLANCE",
    "Your setup, in sight.",
    "Place frame times, performance and toolkit settings where you want them.",
  ],
  spatial: [
    "ROOM FOR YOUR RACE",
    "Your dashboards. In your space.",
    "Place web pages and windows around your view, with a clear resource budget.",
  ],
  bindings: [
    "WITHIN REACH",
    "Your controls. Your toolkit.",
    "Bind profile toggles to your wheel, joystick or buttonbox.",
  ],
  setup: [
    "UNDER THE HOOD",
    "Ready when you are.",
    "Install the OpenXR layer and keep control of your runtime.",
  ],
};
const clone = (value) => JSON.parse(JSON.stringify(value));

export default function App() {
  const [document, setDocument] = useState(() => createDocument());
  const [saved, setSaved] = useState("");
  const [revision, setRevision] = useState(0);
  const [runtime, setRuntime] = useState({
    available: false,
    registered: false,
    active: false,
    message: "Checking runtime…",
  });
  const [backups, setBackups] = useState([]);
  const [tab, setTab] = useState("view");
  const [focusSection, setFocusSection] = useState("");
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState("");
  const [helmetPreparing, setHelmetPreparing] = useState(false);
  const [modules, setModules] = useState(moduleDefaults);
  const [moduleError, setModuleError] = useState("");
  const [moduleSaving, setModuleSaving] = useState(false);
  const [toast, setToast] = useState(null);
  const [dialog, setDialog] = useState(null);
  const [streamImportReport, setStreamImportReport] = useState(null);
  const [newName, setNewName] = useState("");
  const [help, setHelp] = useState(false);
  const [displays, setDisplays] = useState([]);
  const [displayError, setDisplayError] = useState("");
  const [recoveryError, setRecoveryError] = useState("");
  const [pendingNativeError, setPendingNativeError] = useState("");
  const profile =
    document.profiles.find((p) => p.id === document.selectedProfileId) ||
    document.profiles[0];
  const dirty = JSON.stringify(document) !== saved;
  const editorState = useRef({
    dirty: true,
    busy: true,
    profileId: profile.id,
  });
  editorState.current = {
    dirty: dirty || loading || helmetPreparing,
    busy: !!busy || loading || helmetPreparing,
    profileId: profile.id,
  };
  const viewProfile = useMemo(() => modules.stream ? profile : {...profile,stream:{...profile.stream,preserveFullFrame:false}}, [profile,modules.stream]);
  const budget = useMemo(() => pixelBudget(viewProfile), [viewProfile]);
  const maskActive = effectiveMask(viewProfile);
  const framegen = { ...framegenDefaults(), ...profile.framegen };
  const overlay = { ...overlayDefaults(), ...profile.overlay };
  const sharpening = { ...sharpeningDefaults(), ...profile.sharpening };

  async function applyModules(enabled) {
    if (dirty || busy || helmetPreparing || moduleSaving) return;
    setModuleSaving(true);
    try {
      const settings = await api.setModuleSettings({version:1,enabled});
      setModules(settings.enabled);setModuleError(settings.error || "");
      if (settings.enabled[tab] === false) setTab("setup");
      setPendingNativeError(settings.pendingNativeError || "");
      setToast({type:settings.pendingNativeError ? "error" : "success",text:settings.pendingNativeError || "Modules applied. Restart the game to apply native changes. Profile settings are preserved."});
    } catch(error) {setToast({type:"error",text:error.message});}
    finally {setModuleSaving(false);}
  }
  function accept(result) {
    setDocument(result.document);
    setSaved(JSON.stringify(result.document));
    setRevision(result.revision);
    if (result.runtime) setRuntime(result.runtime);
    if (result.backups) setBackups(result.backups);
    setRecoveryError(result.recoveryError || "");
    setPendingNativeError(result.pendingNativeError || "");
  }
  useEffect(() => {
    api.inputEditorState?.(editorState.current).catch(() => {});
  }, [dirty, busy, profile.id, loading, helmetPreparing]);
  useEffect(
    () =>
      api.onInputProfile?.((result) => {
        const current = editorState.current;
        if (
          !current.dirty &&
          !current.busy &&
          result.document?.selectedProfileId === current.profileId
        )
          accept(result);
      }),
    [],
  );
  useEffect(() => {
    let current = true;
    Promise.all([api.load(), api.getModuleSettings().catch(error => ({enabled:Object.fromEntries(Object.keys(moduleDefaults()).map(id=>[id,false])),error:`Could not load module settings: ${error.message}`}))])
      .then(([result, settings]) => {
        if (current) { accept(result); setModules(settings.enabled); setModuleError(settings.error || ""); }
      })
      .catch((error) => {
        if (current) {
          setToast({
            type: "error",
            text: `Could not load profiles: ${error.message}`,
          });
          setRuntime({
            available: false,
            registered: false,
            active: false,
            message: "Profile loading failed. Check Setup before saving.",
          });
        }
      })
      .finally(() => {
        if (current) setLoading(false);
      });
    return () => {
      current = false;
    };
  }, []);
  useEffect(() => {
    let current = true;
    if (tab !== "stream" || !modules.stream) return;
    if (typeof api.listDisplays !== "function") {
      setDisplayError("Display discovery is not available in this build.");
      return;
    }
    api
      .listDisplays()
      .then((items) => {
        if (current) setDisplays(items);
      })
      .catch((error) => {
        if (current)
          setDisplayError(error.message || "Could not discover displays.");
      });
    return () => {
      current = false;
    };
  }, [tab, modules.stream]);
  useEffect(() => {
    const before = (event) => {
      if ((dirty || helmetPreparing || moduleSaving) && !loading) {
        event.preventDefault();
        event.returnValue = "";
      }
    };
    window.addEventListener("beforeunload", before);
    return () => window.removeEventListener("beforeunload", before);
  }, [dirty, loading, helmetPreparing, moduleSaving]);
  useEffect(() => {
    if (!toast || toast.type === "error") return;
    const timer = setTimeout(() => setToast(null), 6500);
    return () => clearTimeout(timer);
  }, [toast]);
  useEffect(() => {
    const escape = (event) => {
      if (event.key === "Escape") {
        setDialog(null);
        setHelp(false);
      }
    };
    window.addEventListener("keydown", escape);
    return () => window.removeEventListener("keydown", escape);
  }, []);
  useEffect(() => {
    if (tab !== "view" || !focusSection) return;
    window.document
      .getElementById(focusSection)
      ?.scrollIntoView({ block: "center" });
    setFocusSection("");
  }, [tab, focusSection]);
  function navigate(tabId, section = "") {
    if (helmetPreparing || moduleSaving) return;
    if (modules[tabId] === false || (section === "headset-sharpening" && !modules.sharpening)) {setTab("setup");return;}
    setTab(tabId);
    setFocusSection(section);
  }
  function writeDraft(update) {
    editorState.current = { ...editorState.current, dirty: true };
    api.inputEditorState?.(editorState.current).catch(() => {});
    setDocument(update);
  }
  function change(update) {
    writeDraft((current) => ({
      ...current,
      profiles: current.profiles.map((item) => {
        if (item.id !== current.selectedProfileId) return item;
        const next = clone(item);
        typeof update === "function"
          ? update(next)
          : Object.assign(next, update);
        return next;
      }),
    }));
  }
  function changeGroup(group, patch) {
    change((next) => {
      const defaults =
        group === "framegen"
          ? framegenDefaults()
          : group === "overlay"
            ? overlayDefaults()
            : group === "sharpening"
              ? sharpeningDefaults()
              : group === "telemetry"
                ? { enabled: false, updateHz: 20 }
                : group === "spatialOverlays"
                  ? spatialDefaults()
                  : group === "centering"
                    ? centeringDefaults()
                    : {};
      next[group] = { ...defaults, ...next[group], ...patch };
      if (group === "spatialOverlays" && patch.items && next.inputBindings) {
        next.inputBindings = pruneSpatialBindings(next).inputBindings;
      }
    });
  }
  function changeExtraArea(side, value) {
    change((next) => {
      next.stream[side] = value;
      if (next.stream.extraSymmetric) {
        next.stream.extraLeft = value;
        next.stream.extraRight = value;
      }
    });
  }
  function setExtraSymmetric(enabled) {
    change((next) => {
      next.stream.extraSymmetric = enabled;
      if (enabled) {
        const extra = Math.max(next.stream.extraLeft, next.stream.extraRight);
        next.stream.extraLeft = extra;
        next.stream.extraRight = extra;
      }
    });
  }
  function changeCrop(edge, value) {
    change((next) => {
      const opposite = {
        left: "right",
        right: "left",
        top: "bottom",
        bottom: "top",
      }[edge];
      next.view.crop[edge] = Math.min(value, 70 - next.view.crop[opposite]);
    });
  }
  async function action(name, callback, success) {
    if (busy) return;
    editorState.current = { ...editorState.current, busy: true };
    api.inputEditorState?.(editorState.current).catch(() => {});
    setBusy(name);
    try {
      const result = await callback();
      if (success) {
        const text = typeof success === "function" ? success(result) : success;
        if (text) setToast({ type: "success", text });
      }
      return result;
    } catch (error) {
      setToast({
        type: "error",
        text: error.message || "Something went wrong. Please try again.",
      });
    } finally {
      setBusy("");
    }
  }
  function save() {
    if (helmetPreparing) return;
    action(
      "save",
      async () => {
        validateDocument(document);
        accept(await api.save(document, revision));
      },
      "Profile saved. Restart your XR session to apply allocation changes.",
    );
  }
  function refreshRuntime() {
    action(
      "refresh",
      async () => {
        const next = await api.runtimeStatus();
        setRuntime(next);
        return next;
      },
      "Runtime status refreshed.",
    );
  }
  function selectGame(game) {
    writeDraft((current) => {
      const existing =
        current.profiles.find(
          (p) => p.game === game && p.id === current.activeProfiles?.[game],
        ) || current.profiles.find((p) => p.game === game);
      if (existing) return { ...current, selectedProfileId: existing.id };
      const next = createProfile(game);
      return {
        ...current,
        selectedProfileId: next.id,
        profiles: [...current.profiles, next],
      };
    });
  }
  function preset(style) {
    const points = contourPreset(style);
    change((next) => {
      next.helmet.style = style;
      next.mask.left = points;
      next.mask.right = points.map((point) => ({ ...point }));
      next.mask.name = `${style === "gt" ? "GT" : style[0].toUpperCase() + style.slice(1)} contour`;
    });
  }
  function launch() {
    action("launch", async () => {
      const result = await api.launchOutput(profile);
      setToast({
        type: result.started ? "success" : "error",
        text:
          result.message ||
          (result.started
            ? "Clean output started."
            : "Clean output could not start."),
      });
      return result;
    });
  }
  function newProfile() {
    const next = createProfile(profile.game, newName.trim() || "New setup");
    writeDraft((current) => ({
      ...current,
      selectedProfileId: next.id,
      profiles: [...current.profiles, next],
    }));
    setDialog(null);
    setToast({
      type: "success",
      text: "New profile created. Save when your setup is ready.",
    });
  }
  function duplicate() {
    const next = clone(profile);
    next.id = createProfile(profile.game).id;
    next.name = `${profile.name} copy`;
    writeDraft((current) => ({
      ...current,
      selectedProfileId: next.id,
      profiles: [...current.profiles, next],
    }));
    setToast({
      type: "success",
      text: "Profile duplicated. Your original setup is preserved.",
    });
  }
  function deleteProfile() {
    writeDraft((current) => {
      const profiles = current.profiles.filter(
        (item) => item.id !== profile.id,
      );
      return { ...current, profiles, selectedProfileId: profiles[0].id };
    });
    setDialog(null);
  }
  function importProfile() {
    action("import", async () => {
      const imported = await api.importProfile();
      if (!imported) return;
      const next = { ...imported, id: createProfile(imported.game).id };
      writeDraft((current) => ({
        ...current,
        selectedProfileId: next.id,
        profiles: [...current.profiles, next],
      }));
      setToast({
        type: "success",
        text: `Imported “${next.name}”. Save to keep this profile.`,
      });
    });
  }
  function importSteadyXr() {
    const game = profile.game;
    action("import-steadyxr", async () => {
      const report = await api.importSteadyXr(game);
      if (!report) return;
      const next = {...report.profile, id:createProfile(game).id};
      writeDraft(current => ({...current, selectedProfileId:next.id, profiles:[...current.profiles,next]}));
      setStreamImportReport({...report, profileId:next.id});
      setTab('stream');
      setToast({type:'success', text:`Imported “${next.name}”. Review the summary and save to keep this profile.`});
    });
  }
  const runtimeLabel = runtime.active
    ? "Session active"
    : runtime.registered
      ? "Layer registered"
      : runtime.available
        ? "Ready to install"
        : "Runtime not installed";

  if (loading)
    return (
      <div className="loading-screen">
        <Logo />
        <span>Preparing your grid…</span>
        <div className="loading-line" />
      </div>
    );

  return (
    <Suspense fallback={<div className="loading-screen" role="status">Loading module…</div>}>
    <BindingsGate enabled={modules.bindings} modules={modules}
      profile={profile}
      dirty={dirty}
      busy={!!busy}
      onChange={(inputBindings) => change({ inputBindings })}
      onToggle={(id) => {
        try {
          change(toggleProfileAction(profile, id));
        } catch (error) {
          setToast({ type: "error", text: error.message });
        }
      }}
    >
      <div className="app-shell" inert={moduleSaving}>
        <aside className="sidebar" inert={!!busy}>
          <button
            className="brand"
            disabled={helmetPreparing}
            onClick={() => navigate("session")}
            aria-label="OpenGrid XR session"
          >
            <Logo />
            <span className="brand-caption">THE SIM RACING XR TOOLKIT</span>
            <span className="byline">
              by <strong>Dan Suzuki</strong>
              <span className="signature-mark">/</span>
            </span>
          </button>
          <div className="nav-section-label">YOUR WORKSPACE</div>
          <nav aria-label="Main navigation">
            {NAV.filter(item => modules[item.id] !== false && (item.id !== 'lmuRendering' || profile.game === 'lmu')).map(({ id, label, icon: Icon }, index) => (
              <React.Fragment key={id}>
                {id === "profiles" && <div className="nav-divider" />}
                <button
                  className={`nav-item ${tab === id ? "active" : ""}`}
                  aria-label={label}
                  disabled={helmetPreparing}
                  onClick={() => navigate(id)}
                  aria-current={tab === id ? "page" : undefined}
                >
                  <Icon size={18} strokeWidth={1.6} />
                  <span>{label}</span>
                  {tab === id && <span className="nav-active-dot" />}
                  {id === "stream" && <span className="nav-mini">OBS</span>}
                </button>
              </React.Fragment>
            ))}
          </nav>
          <div className="sidebar-bottom">
            <SupportPanel disabled={helmetPreparing} />
            <button className="help-button" disabled={helmetPreparing} onClick={() => setHelp(true)}>
              <CircleHelp size={16} />
              <span>Quick start & about</span>
              <ArrowUpRight size={13} />
            </button>
            <div className="sidebar-version">
              <span>OPENGRID XR</span>
              <span>
                v{packageMetadata.version} <i>PREVIEW</i>
              </span>
            </div>
          </div>
        </aside>

        <div className="workspace" inert={!!busy} aria-busy={!!busy}>
          <header className="topbar">
            <div className="game-picker">
              <span className="game-mark">
                <Flag size={17} />
              </span>
              <label className="sr-only" htmlFor="game-select">
                Game
              </label>
              <select
                id="game-select"
                disabled={helmetPreparing}
                value={profile.game}
                onChange={(event) => selectGame(event.target.value)}
              >
                <option value="iracing">iRacing</option>
                <option value="lmu">Le Mans Ultimate</option>
              </select>
              <ChevronDown size={13} />
            </div>
            <span className="topbar-separator" />
            <div className="profile-picker">
              <FolderHeart size={14} />
              <label className="sr-only" htmlFor="profile-select">
                Active profile
              </label>
              <select
                id="profile-select"
                disabled={helmetPreparing}
                value={profile.id}
                onChange={(event) =>
                  writeDraft((current) => ({
                    ...current,
                    selectedProfileId: event.target.value,
                  }))
                }
              >
                {document.profiles
                  .filter((item) => item.game === profile.game)
                  .map((item) => (
                    <option key={item.id} value={item.id}>
                      {item.name}
                    </option>
                  ))}
              </select>
              <ChevronDown size={12} />
            </div>
            <div className="topbar-right">
              <div
                className={`runtime-pill ${runtime.registered ? "ready" : ""}`}
                title={runtime.message}
              >
                <span className="live-dot" />
                {runtimeLabel}
              </div>
              <Button
                variant="primary"
                icon={Save}
                onClick={save}
                disabled={!!busy || !dirty || helmetPreparing}
              >
                {helmetPreparing ? "Adjusting opening…" : busy === "save"
                  ? "Saving…"
                  : dirty
                    ? "Save profile"
                    : "Profile saved"}
              </Button>
            </div>
          </header>

          <main className={`main-content page-${tab}`}>
            {isBrowserPreview && (
              <div className="preview-banner">
                <Monitor size={13} />
                <span>
                  Browser preview · settings are stored in this browser. Open
                  the desktop app to manage the runtime.
                </span>
              </div>
            )}
            {recoveryError && (
              <div className="recovery-banner" role="alert">
                <Info size={16} />
                <div>
                  <strong>Your saved profile file needs recovery.</strong>
                  <p>{recoveryError}</p>
                </div>
                <Button icon={RotateCcw} onClick={() => navigate("profiles")}>
                  View backups
                </Button>
              </div>
            )}
            {pendingNativeError && (
              <div className="recovery-banner" role="alert">
                <Info size={16} />
                <div>
                  <strong>
                    Profile saved; native settings need attention.
                  </strong>
                  <p>{pendingNativeError}</p>
                </div>
                <Button icon={Settings2} onClick={() => navigate("setup")}>
                  Open Setup
                </Button>
              </div>
            )}
            <div className="page-heading">
              <div>
                <span className="eyebrow">{PAGE[tab][0]}</span>
                <h1>{PAGE[tab][1]}</h1>
                <p>{PAGE[tab][2]}</p>
              </div>
            </div>

            <Suspense fallback={<Note>Loading module…</Note>}>
            {tab === "view" && (
              <>
                <div className="view-layout">
                  <div className="view-main">
                    <ContourEditor
                      profile={viewProfile}
                      onMaskChange={(patch) => changeGroup("mask", patch)}
                    />
                    <div className="estimate-strip">
                      <div>
                        <span className="metric-label">
                          {usesFullRenderArea(viewProfile)
                            ? "STREAM FRAME"
                            : "RECTANGULAR VIEW"}
                        </span>
                        <strong>
                          {budget.retainedPercent.toFixed(0)}
                          <small>%</small>
                        </strong>
                        <span>of the original area</span>
                      </div>
                      <div>
                        <span className="metric-label">SUPERSAMPLING</span>
                        <strong>
                          {profile.view.renderScale}
                          <small>%</small>
                        </strong>
                        <span>per image dimension</span>
                      </div>
                      <div>
                        <span className="metric-label">CONTOUR</span>
                        <strong
                          className={maskActive ? "violet-text" : "muted-text"}
                        >
                          {profile.view.disableCrop
                            ? "Bypassed"
                            : maskActive
                            ? "Enabled"
                            : usesFullRenderArea(viewProfile)
                              ? "Preserved"
                              : "Preview"}
                        </strong>
                        <span>
                          {profile.view.disableCrop
                            ? "crop and mask disabled"
                            : usesFullRenderArea(viewProfile)
                            ? "full stream area protected"
                            : maskActive
                              ? "if the game supports masks"
                              : "enable to reduce the game view"}
                        </span>
                      </div>
                    </div>
                    <Note>
                      {usesFullRenderArea(viewProfile)
                        ? "Streaming crop frames the output; it does not reduce headset rendering. Extra area and reserves keep stabilization supplied."
                        : "Geometry estimates only. Performance depends on your headset, game and runtime."}{" "}
                      Save changes, then restart your XR session.
                    </Note>
                  </div>
                  <div className="settings-stack">
                    <Panel
                      id="view-boundaries"
                      title={
                        usesFullRenderArea(viewProfile)
                          ? "Stream framing crop"
                          : "Game render crop"
                      }
                      icon={ScanLine}
                      action={
                        <button
                          className="icon-button"
                          title="Reset view boundaries"
                          aria-label="Reset view boundaries"
                          onClick={() =>
                            change((next) => {
                              next.view.crop = {
                                left: 0,
                                right: 0,
                                top: 0,
                                bottom: 0,
                              };
                            })
                          }
                        >
                          <RotateCcw size={13} />
                        </button>
                      }
                    >
                      <Toggle label="Disable crop" checked={profile.view.disableCrop===true} onChange={disableCrop=>changeGroup('view',{disableCrop})} description="Bypass rectangular crop and contour masking without clearing saved values. Helmet rendering stays independent. Save and restart the sim to apply."/>
                      <p className="panel-description">
                        {usesFullRenderArea(viewProfile)
                          ? "Frame the stream. Keep the headset view intact."
                          : "Trim the edges. Keep your focus."}
                      </p>
                      <fieldset disabled={profile.view.disableCrop===true} style={{border:0,padding:0,margin:0}}><div className="crop-fields">
                        {["left", "right", "top", "bottom"].map((edge) => (
                          <Stepper
                            key={edge}
                            label={edge[0].toUpperCase() + edge.slice(1)}
                            value={profile.view.crop[edge]}
                            onChange={(value) => changeCrop(edge, value)}
                            max={Math.min(
                              40,
                              70 -
                                profile.view.crop[
                                  {
                                    left: "right",
                                    right: "left",
                                    top: "bottom",
                                    bottom: "top",
                                  }[edge]
                                ],
                            )}
                          />
                        ))}
                      </div></fieldset>
                      <div className="panel-divider" />
                      <Range
                        label="Supersampling / render scale"
                        value={profile.view.renderScale}
                        min={50}
                        max={200}
                        onChange={(renderScale) =>
                          changeGroup("view", { renderScale })
                        }
                        hint="100% is the native per-axis target. 200% means 4× the pixel area. This layer scale combines with the game’s render scale; the runtime determines actual resolution."
                      />
                    </Panel>
                    <Panel title="Custom visibility mask" icon={Aperture}>
                      <Toggle
                        label="Use contour in driving"
                        bindingAction="mask.enabled"
                        checked={profile.mask.enabled}
                        onChange={(enabled) => changeGroup("mask", { enabled })}
                      />
                      <div className="preset-label">START WITH A SHAPE</div>
                      <div className="shape-presets">
                        {["formula", "gt", "open"].map((style) => (
                          <button
                            key={style}
                            className={
                              profile.helmet.style === style ? "selected" : ""
                            }
                            onClick={() => preset(style)}
                          >
                            <ShapeIcon style={style} />
                            <span>
                              {style === "gt"
                                ? "GT"
                                : style[0].toUpperCase() + style.slice(1)}
                            </span>
                          </button>
                        ))}
                      </div>
                      <Range
                        label="Edge padding"
                        value={profile.mask.padding}
                        max={10}
                        onChange={(padding) => changeGroup("mask", { padding })}
                      />
                      <div
                        className={`inline-status ${usesFullRenderArea(viewProfile) ? "amber" : ""}`}
                      >
                        <ShieldCheck size={13} />
                        {profile.view.disableCrop
                          ? "Contour saved; rendering bypassed by Disable crop."
                          : usesFullRenderArea(viewProfile)
                          ? "Full render area for stream keeps the output visible."
                          : "Your shape stays local to this profile."}
                      </div>
                    </Panel>
                    {modules.sharpening && (<Panel
                      title="Headset sharpening"
                      icon={Eye}
                      id="headset-sharpening"
                    >
                      <Segmented
                        label="Sharpening mode"
                        value={sharpening.mode}
                        onChange={(mode) => changeGroup("sharpening", { mode })}
                        options={[
                          { value: "off", label: "Off" },
                          { value: "luma", label: "Luma" },
                          { value: "cas", label: "CAS" },
                        ]}
                      />
                      <Range
                        label="Sharpening strength"
                        value={sharpening.strength}
                        disabled={sharpening.mode === "off"}
                        onChange={(strength) =>
                          changeGroup("sharpening", { strength })
                        }
                      />
                      {sharpening.mode === "luma" && (
                        <>
                          <Range
                            label="Luma clamp"
                            min={0}
                            max={0.1}
                            step={0.005}
                            unit=""
                            value={sharpening.clamp}
                            onChange={(clamp) =>
                              changeGroup("sharpening", { clamp })
                            }
                          />
                          <Range
                            label="Luma radius"
                            min={0.1}
                            max={6}
                            step={0.1}
                            unit=""
                            value={sharpening.radius}
                            onChange={(radius) =>
                              changeGroup("sharpening", { radius })
                            }
                          />
                          <Range
                            label="Luma pattern"
                            min={1}
                            max={4}
                            unit=""
                            value={sharpening.pattern}
                            onChange={(pattern) =>
                              changeGroup("sharpening", {
                                pattern: Math.round(pattern),
                              })
                            }
                          />
                        </>
                      )}
                      <Note>
                        Applied to the headset image by the included engine.
                        Save and restart to apply; this fit guide does not
                        simulate sharpening.
                      </Note>
                    </Panel>)}
                  </div>
                </div>
              </>
            )}

            {tab === "session" && (
              <div className="session-layout">
                <section className="session-hero">
                  <div className="session-hero-copy">
                    <span className="eyebrow">
                      {GAME_NAMES[profile.game].toUpperCase()}
                    </span>
                    <h2>
                      Your next lap
                      <br />
                      starts here.
                    </h2>
                    <p>
                      {profile.name}
                      <br />A setup that feels like yours.
                    </p>
                    <Button
                      variant="primary"
                      icon={ScanLine}
                      onClick={() => navigate("view")}
                    >
                      Fine-tune your view <ArrowRight size={14} />
                    </Button>
                  </div>
                  <svg viewBox="0 0 800 450" aria-hidden="true">
                    <TrackArt id="session-art" />
                  </svg>
                  <span className="art-label">
                    ORIGINAL CONCEPT / FIT PREVIEW
                  </span>
                </section>
                <div className="session-cards">
                  <Panel title="Runtime" icon={Activity}>
                    <div className="status-value">
                      <span
                        className={`live-dot ${runtime.registered ? "green" : ""}`}
                      />
                      {runtimeLabel}
                    </div>
                    <p className="panel-description">{runtime.message}</p>
                    <Button onClick={() => navigate("setup")} icon={Settings2}>
                      Runtime setup
                    </Button>
                  </Panel>
                  <Panel title="Current setup" icon={Layers}>
                    <dl className="detail-list">
                      <div>
                        <dt>Rectangular view</dt>
                        <dd>{budget.retainedPercent.toFixed(0)}%</dd>
                      </div>
                      <div>
                        <dt>Supersampling / render scale</dt>
                        <dd>{profile.view.renderScale}%</dd>
                      </div>
                      <div>
                        <dt>Custom contour</dt>
                        <dd>{maskActive ? "Enabled" : "Inactive"}</dd>
                      </div>
                      <div>
                        <dt>Output</dt>
                        <dd>
                          {modules.stream ? `${profile.stream.width} × ${profile.stream.height}` : "Module disabled"}
                        </dd>
                      </div>
                    </dl>
                    <Button
                      onClick={() => navigate("profiles")}
                      icon={FolderHeart}
                    >
                      Manage profiles
                    </Button>
                  </Panel>
                  <Panel title="Before you drive" icon={ShieldCheck}>
                    <ol className="checklist">
                      <li>
                        <span>01</span>Save this profile.
                      </li>
                      <li>
                        <span>02</span>Check the layer in Setup.
                      </li>
                      <li>
                        <span>03</span>Restart your OpenXR session.
                      </li>
                    </ol>
                    <p className="panel-description">
                      Game and headset validation is required. Registration
                      alone does not confirm an active session.
                    </p>
                  </Panel>
                </div>
                {modules.stream && <Note tone="violet">
                  Choose Keep full render area for stream on the Stream page to
                  preserve the complete view and use extra area. Save a separate
                  profile for each setup you prefer.
                </Note>}
              </div>
            )}

            {tab === 'colorCorrection' && <ColorCorrectionPanel key={profile.id} profile={profile} onChange={patch=>changeGroup('colorCorrection',patch)} dirty={dirty} busy={!!busy}/>}
            {tab === "helmet" && (
              <HelmetEditor profile={modules.colorCorrection === false ? {...viewProfile,colorCorrection:{...profile.colorCorrection,enabled:false}} : viewProfile}
                onPreparingChange={setHelmetPreparing}
                onOpeningCommit={(id, helmetPatch, result) => change(next => {
                  if (next.id !== id) return;
                  Object.assign(next.helmet, helmetPatch);
                  Object.assign(next.mask, { enabled: true, linked: true, left: result.contours.left, right: result.contours.right, name: '2D helmet aperture' });
                  next.view.crop = result.crop;
                })}
                onHelmetChange={(patch) => changeGroup("helmet", patch)}
                onMaskChange={(patch) => changeGroup("mask", patch)}
                onViewChange={(patch) => changeGroup("view", patch)} />
            )}
            {tab === "stream" && <StreamPage profile={profile} dirty={dirty} busy={busy} launch={launch} changeGroup={changeGroup} change={change} displays={displays} displayError={displayError} setExtraSymmetric={setExtraSymmetric} changeExtraArea={changeExtraArea}
              importSteadyXr={importSteadyXr} importReport={streamImportReport?.profileId === profile.id ? streamImportReport : null}
              dismissImportReport={() => setStreamImportReport(null)} helmetAvailable={modules.helmet !== false} />}

            {tab === "profiles" && (
              <div className="profiles-layout">
                <div>
                  <div className="section-toolbar">
                    <h2>
                      Your garage{" "}
                      <span>{document.profiles.length} profiles</span>
                    </h2>
                    <div>
                      <Button
                        icon={Upload}
                        onClick={importProfile}
                        disabled={!!busy}
                      >
                        Import
                      </Button>
                      <Button
                        icon={Plus}
                        variant="primary"
                        onClick={() => {
                          setNewName("");
                          setDialog("new");
                        }}
                      >
                        New profile
                      </Button>
                    </div>
                  </div>
                  <div className="profile-cards">
                    {document.profiles.map((item) => (
                      <button
                        key={item.id}
                        className={`profile-card ${item.id === profile.id ? "selected" : ""}`}
                        onClick={() =>
                          writeDraft((current) => ({
                            ...current,
                            selectedProfileId: item.id,
                          }))
                        }
                      >
                        <span className="profile-card-icon">
                          <Flag size={22} />
                        </span>
                        <div>
                          <span className="eyebrow">
                            {GAME_NAMES[item.game]}
                          </span>
                          <h3>{item.name}</h3>
                          <p>
                            {item.view.renderScale}%
                            supersampling
                          </p>
                        </div>
                        <span className="profile-card-check">
                          {item.id === profile.id ? (
                            <Check size={14} />
                          ) : (
                            <ArrowUpRight size={15} />
                          )}
                        </span>
                      </button>
                    ))}
                  </div>
                  <Panel
                    title="Saved backups"
                    icon={RotateCcw}
                    className="backup-panel"
                  >
                    <p className="panel-description">
                      Each save preserves your previous file. Restore a backup
                      to recover a complete profile collection.
                    </p>
                    {backups.length ? (
                      backups.map((backup) => (
                        <div className="backup-row" key={backup.id}>
                          <div>
                            <strong>{backup.name || "Profile backup"}</strong>
                            <span>
                              {backup.createdAt
                                ? new Date(backup.createdAt).toLocaleString()
                                : backup.id}
                            </span>
                          </div>
                          <Button
                            disabled={!!busy}
                            onClick={() =>
                              action(
                                "restore",
                                async () =>
                                  accept(
                                    await api.restoreBackup(
                                      backup.id,
                                      revision,
                                    ),
                                  ),
                                "Backup restored. Your profiles have been recovered.",
                              )
                            }
                            icon={RotateCcw}
                          >
                            Restore
                          </Button>
                        </div>
                      ))
                    ) : (
                      <div className="empty-state">
                        <ShieldCheck size={24} />
                        <p>
                          Your first backup appears after the next saved change.
                        </p>
                      </div>
                    )}
                  </Panel>
                </div>
                <div className="settings-stack">
                  <Panel title="Selected profile" icon={FolderHeart}>
                    <div className="text-field">
                      <label htmlFor="profile-name">Profile name</label>
                      <input
                        id="profile-name"
                        maxLength={80}
                        value={profile.name}
                        onChange={(event) =>
                          change({ name: event.target.value })
                        }
                      />
                    </div>
                    <dl className="detail-list">
                      <div>
                        <dt>Game</dt>
                        <dd>{GAME_NAMES[profile.game]}</dd>
                      </div>
                      <div>
                        <dt>Storage</dt>
                        <dd>Local</dd>
                      </div>
                    </dl>
                    <div className="vertical-actions">
                      <Button icon={Copy} onClick={duplicate}>
                        Duplicate profile
                      </Button>
                      <Button
                        icon={Download}
                        onClick={() =>
                          action(
                            "export",
                            () => api.exportProfile(profile),
                            (result) => (result ? "Profile exported." : null),
                          )
                        }
                        disabled={!!busy}
                      >
                        Export as JSON
                      </Button>
                      <Button
                        icon={Trash2}
                        variant="danger"
                        disabled={document.profiles.length <= 1}
                        onClick={() => setDialog("delete")}
                      >
                        Delete profile
                      </Button>
                    </div>
                  </Panel>
                  <Note>
                    Profiles include your view, contour, helmet preferences and
                    stream settings. Imported settings are validated before use.
                  </Note>
                </div>
              </div>
            )}

            {tab === "telemetry" && (
              <TelemetryPanel
                profile={profile}
                onChange={(patch) => changeGroup("telemetry", patch)}
              />
            )}
            {tab === "benchmark" && <BenchmarkPanel profile={profile} dirty={dirty||Boolean(busy)} />}
            {tab === "bindings" && <InputBindingsPage />}
            {tab === "layers" && <LayersPage />}
            {tab === 'lmuRendering' && <QuadViewPage profile={profile} onChange={patch=>changeGroup('lmuRendering',patch)} onViewChange={patch=>changeGroup('view',patch)} revision={revision} dirty={dirty} busy={busy} run={action} onNavigate={navigate}/>}


            {tab === "framegen" && (
              <FrameGenerationPage
                framegen={framegen}
                overlay={overlay}
                centeringShortcut={profile.centering?.gameShortcut || ""}
                onChange={(patch) => changeGroup("framegen", patch)}
                runtime={runtime}
                onRefresh={refreshRuntime}
                onSetup={() => navigate("setup")}
                onOpenVectorXR={() => action("project", () => api.openVectorXR())}
              />
            )}
            {tab === "overlay" && (
              <OverlayPage
                overlay={overlay}
                framegen={framegen}
                profile={viewProfile}
                gameName={GAME_NAMES[profile.game]}
                onChange={(patch) => changeGroup("overlay", patch)}
                runtime={runtime}
                onRefresh={refreshRuntime}
              />
            )}
            {tab === "spatial" && (
              <SpatialOverlaysPanel
                profile={profile}
                gameName={GAME_NAMES[profile.game]}
                onChange={(patch) => changeGroup("spatialOverlays", patch)}
                onCenterChange={(patch) => changeGroup("centering", patch)}
                run={action}
                busy={busy}
                dirty={dirty}
              />
            )}

            </Suspense>
            <UpdatesPanel details={tab === "setup"} dirty={dirty} busy={busy} />
            {tab === "setup" && <ModulesPanel enabled={modules} error={moduleError} saving={moduleSaving} blocked={dirty || !!busy || helmetPreparing} onApply={applyModules} />}
            {tab === "setup" && (
              <div className="setup-layout">
                <Panel
                  title="OpenXR runtime layer"
                  icon={Box}
                  action={
                    <span
                      className={`badge ${runtime.registered ? "good" : ""}`}
                    >
                      {runtime.registered ? "REGISTERED" : "NOT REGISTERED"}
                    </span>
                  }
                  className="runtime-panel"
                >
                  <div className="runtime-intro">
                    <span className="runtime-symbol">
                      <Layers size={30} />
                    </span>
                    <div>
                      <h3>OpenGrid XR Layer</h3>
                      <p>
                        Per-game view crop, resolution recommendations and
                        custom visibility contours.
                      </p>
                    </div>
                  </div>
                  <div className="runtime-message">
                    <span
                      className={`live-dot ${runtime.active ? "green" : ""}`}
                    />
                    <p>{runtime.message}</p>
                  </div>
                  <dl className="detail-list">
                    <div>
                      <dt>Native files</dt>
                      <dd>
                        {runtime.available ? "Available" : "Not available"}
                      </dd>
                    </div>
                    <div>
                      <dt>Layer registration</dt>
                      <dd>
                        {runtime.registered ? "Registered" : "Not registered"}
                      </dd>
                    </div>
                    <div>
                      <dt>Active XR session</dt>
                      <dd>{runtime.active ? "Active" : "Not confirmed"}</dd>
                    </div>
                    <div>
                      <dt>Game compatibility</dt>
                      <dd>Headset validation required</dd>
                    </div>
                  </dl>
                  <div className="runtime-actions">
                    <Button
                      variant="primary"
                      icon={Download}
                      disabled={!!busy || isBrowserPreview}
                      onClick={() =>
                        action(
                          "install",
                          async () => {
                            const next = await api.installRuntime();
                            setRuntime(next);
                            return next;
                          },
                          (result) => result.message,
                        )
                      }
                    >
                      {busy === "install"
                        ? "Installing…"
                        : runtime.registrationRepairRequired
                          ? "Repair runtime connection"
                        : runtime.registered
                          ? "Update runtime layer"
                          : "Install runtime layer"}
                    </Button>
                    <Button
                      icon={RefreshCw}
                      disabled={!!busy}
                      onClick={() =>
                        action(
                          "refresh",
                          async () => {
                            const next = await api.runtimeStatus();
                            setRuntime(next);
                            return next;
                          },
                          "Runtime status refreshed.",
                        )
                      }
                    >
                      Refresh
                    </Button>
                    <Button
                      variant="ghost"
                      disabled={
                        !!busy || !(runtime.registrationPresent ?? runtime.registered) || isBrowserPreview
                      }
                      onClick={() =>
                        action(
                          "remove",
                          async () => {
                            const next = await api.removeRuntime();
                            setRuntime(next);
                            return next;
                          },
                          (result) => result.message,
                        )
                      }
                    >
                      Remove layer
                    </Button>
                  </div>
                  <Note>
                    Installation registers OpenGrid XR for all users and opens a Windows
                    administrator prompt. It does not replace your
                    chosen OpenXR runtime. Restart the game after installation
                    or profile changes.
                  </Note>
                </Panel>
                <div className="settings-stack">
                  <Panel title="LMU offline test" icon={Gamepad2}>
                    <p className="panel-description">
                      Test unsigned OpenGrid XR DLLs in an offline Race Weekend.
                      Select the LMU game EXE to launch it directly in OpenXR
                      (+XR), without EAC. Keep Steam and your VR runtime running.
                    </p>
                    <Button
                      icon={Gamepad2}
                      disabled={isBrowserPreview || !!busy || dirty || !runtime.registered}
                      onClick={() => action("lmu-offline", () => api.launchLmuOffline(), result => result?.message)}
                    >
                      {busy === "lmu-offline" ? "Starting LMU…" : "Test offline in LMU"}
                    </Button>
                    <p className="panel-description">
                      Save your profile and install the runtime layer first. Online
                      racing requires restarting LMU normally through Steam with EAC.
                      This does not guarantee DLL compatibility with your VR runtime.
                    </p>
                  </Panel>
                  <Panel title="Local & yours" icon={FolderOpen}>
                    <p className="panel-description">
                      Profiles and backups live in the app’s data folder. No
                      account, subscription or activation.
                    </p>
                    <Button
                      icon={FolderOpen}
                      onClick={() =>
                        action("folder", () => api.openDataFolder())
                      }
                      disabled={isBrowserPreview || !!busy}
                    >
                      Open data folder
                    </Button>
                  </Panel>
                  <Panel title="Included enhancement engine" icon={Layers}>
                    <div className="component-row">
                      <strong>VectorXR</strong>
                      <span className="badge subtle">INTEGRATED / MPL-2.0</span>
                    </div>
                    <p className="panel-description">
                      The integrated VectorXR engine provides optional headset
                      frame generation and the VR overlay. Install or update it
                      with the OpenGrid XR runtime layer. No separate VectorXR
                      application is needed.
                    </p>
                    <div className="engine-file-state">
                      <span
                        className={`live-dot ${runtime.enhancementsAvailable ? "green" : ""}`}
                      />
                      {runtime.enhancementsAvailable
                        ? "Engine files available"
                        : "Engine files not confirmed"}
                    </div>
                    {runtime.updateRequired && (
                      <Note tone="amber">
                        The installed runtime needs an update to match this
                        build.
                      </Note>
                    )}
                  </Panel>
                  <Panel title="Made for the grid" icon={Grid2X2}>
                    <p className="panel-description">
                      OpenGrid XR is source-available for noncommercial use. Native
                      stabilization builds on Horizonlock VR, with source
                      attribution included.
                    </p>
                    <div className="license-line">
                      <span>POLYFORM NONCOMMERCIAL 1.0.0</span>
                      <span>NO EXPIRY</span>
                      <span>NO PAYWALL</span>
                    </div>
                  </Panel>
                </div>
              </div>
            )}
          </main>
          <footer className="statusbar">
            <span>
              <span className={`live-dot ${dirty ? "amber" : "green"}`} />
              {dirty ? "Unsaved changes" : "All changes saved locally"}
            </span>
            <span className="statusbar-middle">
              <Info size={11} /> Changes apply after XR session restart
            </span>
            <span>
              {GAME_NAMES[profile.game]}
            </span>
          </footer>
        </div>

        {toast && (
          <div
            className={`toast ${toast.type}`}
            role={toast.type === "error" ? "alert" : "status"}
          >
            {toast.type === "error" ? (
              <Info size={18} />
            ) : (
              <CheckCircle2 size={18} />
            )}
            <span>{toast.text}</span>
            <button aria-label="Dismiss message" onClick={() => setToast(null)}>
              <X size={15} />
            </button>
          </div>
        )}
        {dialog && (
          <div
            className="modal-backdrop"
            inert={!!busy}
            onMouseDown={(event) => {
              if (event.target === event.currentTarget) setDialog(null);
            }}
          >
            <section
              role="dialog"
              aria-modal="true"
              aria-labelledby="dialog-title"
              className="modal"
            >
              <button
                className="modal-close icon-button"
                aria-label="Close dialog"
                onClick={() => setDialog(null)}
              >
                <X size={18} />
              </button>
              <span className="eyebrow">YOUR GARAGE</span>
              <h2 id="dialog-title">
                {dialog === "new"
                  ? "A fresh starting point."
                  : "Delete this profile?"}
              </h2>
              {dialog === "new" ? (
                <>
                  <p>
                    Create a neutral setup for {GAME_NAMES[profile.game]}. Your
                    other profiles stay exactly as they are.
                  </p>
                  <form
                    onSubmit={(event) => {
                      event.preventDefault();
                      newProfile();
                    }}
                  >
                    <div className="text-field">
                      <label htmlFor="new-profile-name">Profile name</label>
                      <input
                        id="new-profile-name"
                        autoFocus
                        maxLength={80}
                        placeholder="e.g. GT3 · race day"
                        value={newName}
                        onChange={(event) => setNewName(event.target.value)}
                      />
                    </div>
                    <div className="modal-actions">
                      <Button type="button" onClick={() => setDialog(null)}>
                        Cancel
                      </Button>
                      <Button type="submit" variant="primary" icon={Plus}>
                        Create profile
                      </Button>
                    </div>
                  </form>
                </>
              ) : (
                <>
                  <p>
                    “{profile.name}” will be removed from this collection when
                    you save. Existing backups remain available.
                  </p>
                  <div className="modal-actions">
                    <Button onClick={() => setDialog(null)}>
                      Keep profile
                    </Button>
                    <Button
                      variant="danger"
                      icon={Trash2}
                      onClick={deleteProfile}
                    >
                      Delete profile
                    </Button>
                  </div>
                </>
              )}
            </section>
          </div>
        )}
        {help && (
          <div
            className="modal-backdrop"
            inert={!!busy}
            onMouseDown={(event) => {
              if (event.target === event.currentTarget) setHelp(false);
            }}
          >
            <section
              className="modal help-modal"
              role="dialog"
              aria-modal="true"
              aria-labelledby="help-title"
            >
              <button
                className="modal-close icon-button"
                aria-label="Close help"
                onClick={() => setHelp(false)}
              >
                <X size={18} />
              </button>
              <Logo />
              <h2 id="help-title">Your first lap with OpenGrid.</h2>
              <ol className="help-steps">
                <li>
                  <strong>Pick your game & profile.</strong>
                  <span>
                    Each setup keeps its own view, mask and stream preferences.
                  </span>
                </li>
                <li>
                  <strong>Shape what you see.</strong>
                  <span>
                    Start with a preset, then drag contour points. Arrow keys
                    fine-tune; Shift moves farther. A custom mask is optional.
                  </span>
                </li>
                <li>
                  <strong>Save, install, restart.</strong>
                  <span>
                    Save your profile and install the layer in Setup. Restart
                    your OpenXR game. Validate behavior in your headset.
                  </span>
                </li>
                <li>
                  <strong>Give your audience a clear view.</strong>
                  <span>
                    Keep full render area for stream preserves the complete frame. Open the clean
                    output and add it as a window capture in OBS.
                  </span>
                </li>
              </ol>
              <Note>
                This is an early source-available build. Preview artwork is original;
                helmet assets are not yet rendered in-headset. No hardware
                performance values are simulated.
              </Note>
              <Button variant="primary" onClick={() => setHelp(false)}>
                Back to the grid <ArrowRight size={14} />
              </Button>
            </section>
          </div>
        )}
        {busy && (
          <div className="operation-overlay">
            <div className="operation-status" role="status" aria-live="polite">
              <span className="operation-spinner" aria-hidden="true" />
              <div>
                <strong>
                  {BUSY_LABELS[busy] || "Completing your request…"}
                </strong>
                <p>Editing resumes when this operation is complete.</p>
              </div>
            </div>
          </div>
        )}
      </div>
    </BindingsGate>
    </Suspense>
  );
}

function Logo() {
  return (
    <div className="logo">
      <svg viewBox="0 0 40 40" aria-hidden="true">
        <path
          d="m9 9 11-4 14 6-4 17-13 5L6 25Z"
          fill="none"
          stroke="currentColor"
          strokeWidth="2.2"
        />
        <path
          d="m9 9 11 10 14-8M20 19l-3 14"
          fill="none"
          stroke="currentColor"
          strokeWidth="2.2"
        />
      </svg>
      <span>
        OpenGrid<span className="xr">XR</span>
      </span>
    </div>
  );
}
function ShapeIcon({ style }) {
  return (
    <svg viewBox="0 0 60 32" aria-hidden="true">
      <path
        d={
          style === "formula"
            ? "M5 12 13 6 47 6 55 12 50 25 38 28 30 22 22 28 10 25Z"
            : style === "gt"
              ? "M5 11Q30 0 55 11L51 25Q30 33 9 25Z"
              : "M7 5H53V27H7Z"
        }
        fill="currentColor"
        fillOpacity=".12"
        stroke="currentColor"
        strokeWidth="1.3"
      />
    </svg>
  );
}

function BindingsGate({enabled,children,...props}) {
  return enabled ? <InputBindingsProvider {...props}>{children}</InputBindingsProvider> : children;
}

import React, { useEffect, useId, useRef, useState } from "react";
import {
  Crosshair,
  Link2,
  Maximize,
  MousePointer2,
  Plus,
  Minus,
  RotateCcw,
  Unlink2,
} from "lucide-react";
import { isSimplePolygon, polygonArea, contourPreset } from "../model.mjs";
import { Button, Segmented } from "./Controls.jsx";
import { BindingButton, useInputBindings } from "./InputBindings.jsx";

const box = { x: 42, y: 38, w: 716, h: 370 };
const coord = (point) =>
  `${box.x + point.x * box.w},${box.y + point.y * box.h}`;
const clamp = (value) => Math.max(0, Math.min(1, value));

export function TrackArt({ id = "track" }) {
  return (
    <g className="track-art">
      <defs>
        <linearGradient id={`${id}-sky`} x2="0" y2="1">
          <stop stopColor="#20232d" />
          <stop offset="1" stopColor="#3b3b46" />
        </linearGradient>
        <linearGradient id={`${id}-road`} x2="0" y2="1">
          <stop stopColor="#373841" />
          <stop offset="1" stopColor="#13151c" />
        </linearGradient>
        <linearGradient id={`${id}-body`} x2="0" y2="1">
          <stop stopColor="#4a4064" />
          <stop offset=".6" stopColor="#252332" />
          <stop offset="1" stopColor="#111218" />
        </linearGradient>
        <pattern
          id={`${id}-grid`}
          width="40"
          height="40"
          patternUnits="userSpaceOnUse"
        >
          <path
            d="M40 0H0V40"
            fill="none"
            stroke="#ffffff"
            strokeOpacity=".035"
            strokeWidth="1"
          />
        </pattern>
      </defs>
      <rect width="800" height="450" fill={`url(#${id}-sky)`} />
      <path
        d="M0 187 45 168 90 178 130 159 170 172 210 155 260 177 300 167 335 178 390 153 445 178 480 164 525 176 570 151 610 169 655 158 700 173 740 162 800 178V250H0Z"
        fill="#282c35"
      />
      <path d="M0 215 200 207 330 217 440 211 800 193V450H0Z" fill="#242f2e" />
      <path
        d="M360 207C350 252 270 293 97 450H740C535 293 456 251 433 207Z"
        fill={`url(#${id}-road)`}
      />
      <path
        d="M356 210C344 257 266 300 89 450"
        stroke="#a9a8ae"
        strokeWidth="3"
        fill="none"
      />
      <path
        d="M438 210C460 258 550 315 746 450"
        stroke="#a9a8ae"
        strokeWidth="3"
        fill="none"
      />
      <path
        d="M350 216C329 273 263 310 76 450"
        stroke="#84767d"
        strokeWidth="9"
        strokeDasharray="18 16"
        fill="none"
      />
      <path
        d="M445 217C470 271 545 317 760 450"
        stroke="#84767d"
        strokeWidth="9"
        strokeDasharray="18 16"
        fill="none"
      />
      <path
        d="m397 211 1 12m1 15 1 20m1 21 2 36m1 27 2 61"
        stroke="#c4bcae"
        strokeOpacity=".4"
        strokeWidth="2"
      />
      <path
        d="M0 209 325 213M0 226 316 219M800 204 463 214M800 227 472 222"
        fill="none"
        stroke="#65616c"
        strokeWidth="2"
      />
      <path
        d="m63 205 2 19m51-16 2 15m52-12 2 12m51-11 2 10m51-8 2 9m401-15-1 16m-51-13-1 14m-51-10-1 12m-51-8-1 10"
        stroke="#79727d"
        strokeWidth="2"
      />
      <g stroke="#57535f" strokeWidth="2" fill="none">
        <path d="M305 214v-40h184v40M306 181h182" />
        <path d="m319 174 8 7 8-7 8 7 8-7 8 7 8-7 8 7 8-7 8 7 8-7 8 7 8-7 8 7 8-7 8 7 8-7 8 7" />
      </g>
      <path
        d="M192 450C195 376 214 341 267 330L347 359H451L531 330C585 341 604 376 608 450Z"
        fill="#101118"
        stroke="#494453"
        strokeWidth="2"
      />
      <path
        d="M285 450 312 349 357 327H439L487 349 516 450"
        fill={`url(#${id}-body)`}
        stroke="#726187"
        strokeWidth="1.5"
      />
      <path
        d="M357 327 375 260H421L439 327 415 346H380Z"
        fill="#211e2d"
        stroke="#6b597b"
        strokeWidth="1.5"
      />
      <path d="M390 266h14l7 57h-28Z" fill="#75628f" opacity=".5" />
      <path
        d="M218 392 274 382 300 410M582 392 527 382 501 410"
        fill="none"
        stroke="#56505e"
        strokeWidth="5"
      />
      <path
        d="M307 450C310 414 336 397 366 397H432C462 397 488 414 491 450"
        fill="#0e0f14"
        stroke="#58505f"
        strokeWidth="9"
      />
      <rect
        x="371"
        y="417"
        width="57"
        height="27"
        rx="4"
        fill="#262c32"
        stroke="#66606e"
      />
      <path d="M379 426h40m-40 6h25" stroke="#7a8990" strokeWidth="2" />
      <rect width="800" height="450" fill={`url(#${id}-grid)`} />
    </g>
  );
}

export default function ContourEditor({
  profile,
  onMaskChange,
  compact = false,
}) {
  const bindings = useInputBindings();
  const [eye, setEye] = useState("left");
  const [selected, setSelected] = useState(0);
  const [guides, setGuides] = useState(true);
  const [wide, setWide] = useState(false);
  const [notice, setNotice] = useState("");
  useEffect(() => {
    const escape = (event) => {
      if (event.key === "Escape") setWide(false);
    };
    window.addEventListener("keydown", escape);
    return () => window.removeEventListener("keydown", escape);
  }, []);
  const svg = useRef(null);
  const drag = useRef(null);
  const unique = useId().replace(/:/g, "");
  const points = profile.mask[eye];
  const index = Math.min(selected, points.length - 1);
  const polygon = points.map(coord).join(" ");
  const crop = profile.view.crop;
  const commit = (next) => {
    if (!isSimplePolygon(next) || Math.abs(polygonArea(next)) < 0.04) {
      setNotice("Keep the contour open and its edges uncrossed.");
      return false;
    }
    setNotice("");
    onMaskChange({
      [eye]: next,
      ...(profile.mask.linked
        ? { [eye === "left" ? "right" : "left"]: next.map((p) => ({ ...p })) }
        : {}),
      name: "Custom contour",
    });
    return true;
  };
  const move = (at, x, y) =>
    commit(
      points.map((point, i) =>
        i === at ? { x: clamp(x), y: clamp(y) } : point,
      ),
    );
  const startDrag = (event, at) => {
    event.preventDefault();
    setSelected(at);
    drag.current = at;
    svg.current?.setPointerCapture(event.pointerId);
  };
  const pointerMove = (event) => {
    if (drag.current === null) return;
    const matrix = svg.current.getScreenCTM();
    if (!matrix) return;
    const pt = new DOMPoint(event.clientX, event.clientY).matrixTransform(
      matrix.inverse(),
    );
    move(drag.current, (pt.x - box.x) / box.w, (pt.y - box.y) / box.h);
  };
  const addPoint = () => {
    const after = (index + 1) % points.length;
    const next = [...points];
    next.splice(index + 1, 0, {
      x: (points[index].x + points[after].x) / 2,
      y: (points[index].y + points[after].y) / 2,
    });
    if (commit(next)) setSelected(index + 1);
  };
  const removePoint = () => {
    if (points.length <= 3) return;
    if (commit(points.filter((_, i) => i !== index)))
      setSelected(Math.max(0, index - 1));
  };
  const keyMove = (event, at) => {
    if (event.key === "Delete" || event.key === "Backspace") {
      event.preventDefault();
      removePoint();
      return;
    }
    const step = event.shiftKey ? 0.01 : 0.002;
    let p = points[at];
    if (
      ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)
    ) {
      event.preventDefault();
      move(
        at,
        p.x +
          (event.key === "ArrowLeft"
            ? -step
            : event.key === "ArrowRight"
              ? step
              : 0),
        p.y +
          (event.key === "ArrowUp"
            ? -step
            : event.key === "ArrowDown"
              ? step
              : 0),
      );
    }
  };
  const scene = (
    <svg
      ref={svg}
      className="contour-svg"
      viewBox="0 0 800 450"
      preserveAspectRatio="none"
      role="group"
      aria-label={`${eye} eye contour editor`}
      onPointerMove={pointerMove}
      onPointerUp={() => {
        drag.current = null;
      }}
      onPointerCancel={() => {
        drag.current = null;
      }}
    >
      <defs>
        <mask id={`${unique}-cut`}>
          <rect width="800" height="450" fill="white" />
          <polygon points={polygon} fill="black" />
        </mask>
        <linearGradient id={`${unique}-line`}>
          <stop stopColor="#c4a2ff" />
          <stop offset=".5" stopColor="#9b89f3" />
          <stop offset="1" stopColor="#ddc6ff" />
        </linearGradient>
      </defs>
      <TrackArt id={unique} />
      <rect
        width="800"
        height="450"
        fill="#0b0b12"
        opacity=".79"
        mask={`url(#${unique}-cut)`}
      />
      <polygon
        points={polygon}
        fill="none"
        stroke="#7761de"
        strokeWidth="15"
        opacity=".06"
      />
      <polygon
        points={polygon}
        fill="none"
        stroke={`url(#${unique}-line)`}
        strokeWidth="2"
      />
      {guides && (
        <g pointerEvents="none">
          <rect
            x={box.x}
            y={box.y}
            width={box.w}
            height={box.h}
            rx="1"
            stroke="#9a91b5"
            strokeOpacity=".25"
            strokeDasharray="4 5"
            fill="none"
          />
          <path
            d="M400 38v370M42 223h716"
            stroke="#c4badc"
            strokeOpacity=".2"
            strokeDasharray="3 7"
          />
          <path d="M390 223h20m-10-10v20" stroke="#ddd2ff" strokeOpacity=".7" />
          <text x="681" y="29" fill="#777383" fontSize="9" letterSpacing="1">
            {eye.toUpperCase()} EYE
          </text>
        </g>
      )}
      {crop.left + crop.right + crop.top + crop.bottom > 0 && (
        <rect
          x={box.x + (crop.left / 100) * box.w}
          y={box.y + (crop.top / 100) * box.h}
          width={box.w * (1 - (crop.left + crop.right) / 100)}
          height={box.h * (1 - (crop.top + crop.bottom) / 100)}
          fill="none"
          stroke="#e6ba78"
          strokeOpacity=".8"
          strokeDasharray="6 4"
          strokeWidth="1.2"
          pointerEvents="none"
        />
      )}
      {points.map((point, at) => (
        <g key={at}>
          <circle
            cx={box.x + point.x * box.w}
            cy={box.y + point.y * box.h}
            r={at === index ? 12 : 9}
            fill={at === index ? "#a792ff" : "#7059dd"}
            opacity={at === index ? 0.19 : 0.08}
          />
          <circle
            className={`control-point ${at === index ? "active" : ""}`}
            cx={box.x + point.x * box.w}
            cy={box.y + point.y * box.h}
            r={at === index ? 5 : 3.8}
            fill={at === index ? "#e1d9ff" : "#8d77e1"}
            stroke={at === index ? "#fff" : "#d8cbff"}
            strokeWidth="1.3"
            role="button"
            aria-label={`Contour point ${at + 1}`}
            tabIndex="0"
            onFocus={() => setSelected(at)}
            onPointerDown={(event) => startDrag(event, at)}
            onKeyDown={(event) => keyMove(event, at)}
          />
        </g>
      ))}
      <text
        x="400"
        y="437"
        textAnchor="middle"
        fill="#8f869f"
        fontSize="9"
        letterSpacing="2.5"
      >
        ORIGINAL FIT GUIDE · NOT AN IN-HEADSET RENDER
      </text>
    </svg>
  );
  return (
    <section
      className={`contour-panel ${compact ? "compact" : ""} ${wide ? "expanded" : ""}`}
    >
      <div className="preview-top">
        <div className="preview-heading">
          <span className="live-dot violet" />
          <h2>Make the view your own.</h2>
          <span className="badge subtle">CONTOUR EDITOR</span>
        </div>
        <div className="preview-tools">
          <button
            title="Toggle guides"
            aria-label="Toggle guides"
            aria-pressed={guides}
            onClick={() => setGuides(!guides)}
            className={guides ? "active" : ""}
          >
            <Crosshair size={16} />
          </button>
          <button
            title={wide ? "Close expanded editor" : "Expand editor"}
            aria-label={wide ? "Close expanded editor" : "Expand editor"}
            onClick={() => setWide(!wide)}
          >
            <Maximize size={15} />
          </button>
        </div>
      </div>
      <div className="eye-toolbar">
        <Segmented
          label="Eye to edit"
          value={eye}
          onChange={(value) => {
            setEye(value);
            setSelected(0);
          }}
          options={[
            { value: "left", label: "Left eye" },
            { value: "right", label: "Right eye" },
          ]}
        />
        <button
          className={`link-button ${profile.mask.linked ? "active" : ""}`}
          onClick={() =>
            bindings
              ? bindings.toggle("mask.linked")
              : onMaskChange({
                  linked: !profile.mask.linked,
                  ...(!profile.mask.linked
                    ? {
                        [eye === "left" ? "right" : "left"]: points.map(
                          (p) => ({
                            ...p,
                          }),
                        ),
                      }
                    : {}),
                })
          }
        >
          {profile.mask.linked ? <Link2 size={14} /> : <Unlink2 size={14} />}{" "}
          {profile.mask.linked ? "Eyes linked" : "Independent eyes"}
        </button>
        <BindingButton actionId="mask.linked" />
        <span className="points-count">{points.length} POINTS</span>
      </div>
      <div className="scene-container">
        {scene}
        <div className="scene-tag">
          <span className="live-dot violet" /> EDITING{" "}
          {profile.mask.name || "CUSTOM CONTOUR"}
        </div>
      </div>
      <div className="editor-bottom">
        <div className="editor-tip">
          <MousePointer2 size={13} />
          <span>{notice || "Drag a point to shape your field of view"}</span>
        </div>
        <button
          className="text-button"
          onClick={() => {
            const next = contourPreset(profile.helmet.style);
            onMaskChange({
              left: next,
              right: next.map((p) => ({ ...p })),
              name: `${profile.helmet.style === "gt" ? "GT" : profile.helmet.style[0].toUpperCase() + profile.helmet.style.slice(1)} contour`,
            });
            setSelected(0);
            setNotice("");
          }}
        >
          <RotateCcw size={12} /> Reset shape
        </button>
      </div>
      <div className="point-inspector">
        <span className="point-index">
          POINT {String(index + 1).padStart(2, "0")}
        </span>
        <label>
          X{" "}
          <input
            aria-label="Selected point X"
            type="number"
            value={Number((points[index].x * 100).toFixed(1))}
            min="0"
            max="100"
            step=".1"
            onChange={(event) => {
              const n = Number(event.target.value);
              if (Number.isFinite(n)) move(index, n / 100, points[index].y);
            }}
          />
          <span>%</span>
        </label>
        <label>
          Y{" "}
          <input
            aria-label="Selected point Y"
            type="number"
            value={Number((points[index].y * 100).toFixed(1))}
            min="0"
            max="100"
            step=".1"
            onChange={(event) => {
              const n = Number(event.target.value);
              if (Number.isFinite(n)) move(index, points[index].x, n / 100);
            }}
          />
          <span>%</span>
        </label>
        <div className="point-actions">
          <button
            title="Add point after selected point"
            aria-label="Add contour point"
            onClick={addPoint}
            disabled={points.length >= 64}
          >
            <Plus size={13} />
          </button>
          <button
            title="Remove selected point"
            aria-label="Remove contour point"
            onClick={removePoint}
            disabled={points.length <= 3}
          >
            <Minus size={13} />
          </button>
        </div>
        <span className="keyboard-hint">
          <kbd>↑↓←→</kbd> fine-tune
        </span>
      </div>
    </section>
  );
}

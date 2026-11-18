import { useId } from "react";
import { effectiveCrop } from "../model.mjs";

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

export default function CropPreview({profile}) {
  const id = useId().replace(/:/g, "");
  const crop = effectiveCrop(profile);
  const x = crop.left * 8, y = crop.top * 4.5;
  const width = 800 - x - crop.right * 8;
  const height = 450 - y - crop.bottom * 4.5;
  const cropped = Object.values(crop).some(value => value > 0);
  const edges = [
    crop.left > 0 ? `M${x} ${y}v${height}` : '',
    crop.right > 0 ? `M${x + width} ${y}v${height}` : '',
    crop.top > 0 ? `M${x} ${y}h${width}` : '',
    crop.bottom > 0 ? `M${x} ${y + height}h${width}` : '',
  ].join(' ');
  return <div className="contour-editor" style={{position:'relative',borderRadius:12,overflow:'hidden'}}><svg viewBox="0 0 800 450" role="img" aria-label="Rectangular crop preview" style={{width:'100%',display:'block'}}>
    <TrackArt id={id}/>
    <path d={`M0 0H800V450H0Z M${x} ${y}v${height}h${width}V${y}Z`} fill="black" fillOpacity=".65" fillRule="evenodd"/>
    {cropped && <path d={edges} fill="none" stroke="#b59af7" strokeWidth="2" vectorEffect="non-scaling-stroke"/>}
  </svg><div aria-hidden="true" style={{position:'absolute',inset:0,borderRadius:'inherit',boxShadow:`inset 0 0 0 1px ${cropped ? 'var(--border, #393140)' : '#b59af7'}`,pointerEvents:'none'}}/></div>;
}

"use strict";
(async () => {
  const parameters = new URL(location.href).searchParams;
  const fps = Math.max(1, Math.min(30, Number(parameters.get("fps")) || 10));
  const width = Math.max(256, Math.min(1920, Number(parameters.get("width")) || 1024));
  const height = Math.max(128, Math.min(1080, Number(parameters.get("height")) || 576));
  const fill = parameters.get("fit") === "fill";
  // Desktop input maps the entire texture to the entire monitor. Capture the
  // uncropped monitor and fill the texture, including custom source aspects.
  const videoConstraints = fill ? { resizeMode: "none" } : { width: { ideal: width, max: width }, height: { ideal: height, max: height } };
  const stream = await navigator.mediaDevices.getDisplayMedia({ video: { cursor: "always", frameRate: { ideal: fps, max: fps }, ...videoConstraints }, audio: false });
  for (const track of stream.getVideoTracks()) track.contentHint = "detail";
  const video = document.querySelector("video");
  video.style.objectFit = fill ? "fill" : "contain";
  let ended = false;
  const stop = () => {
    if (ended) return; ended = true;
    stream.getTracks().forEach((track) => track.stop());
    video.srcObject = null;
    document.title = "Capture unavailable";
  };
  stream.getVideoTracks().forEach((track) => track.addEventListener("ended", stop, { once: true }));
  video.srcObject = stream;
  await video.play();
  addEventListener("beforeunload", stop);
})().catch((error) => { document.title = "Capture unavailable"; console.error("Window capture failed:", error.message); });

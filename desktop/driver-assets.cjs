"use strict";
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { stageVerifiedAsset } = require("./wheel-import.cjs");

const spatial = import(pathToFileURL(path.join(__dirname, "../src/spatial-overlays.mjs")).href);
const staging = new Map();

async function stageDriver(model, bundledDirectory, localAppData = process.env.LOCALAPPDATA) {
  const { wheelDriverAsset } = await spatial;
  model = wheelDriverAsset(model);
  if (!model) throw new Error("Unsupported driver wheel model.");
  if (!localAppData) throw new Error("The local wheel cache is unavailable.");
  const directory = path.join(localAppData, "OpenGridXR", "wheels");
  const key = path.join(directory, `${model}.agd`);
  if (staging.has(key)) return staging.get(key);
  const task = stageVerifiedAsset(`${model}.agd`, `${model}.driver.json`, directory, bundledDirectory, "driver").catch(error => {
    if (error.code === "ENOENT") throw new Error("The bundled driver asset or manifest is missing.");
    throw error;
  });
  staging.set(key, task);
  try {
    await task;
  } finally {
    staging.delete(key);
  }
}

module.exports = { stageDriver };

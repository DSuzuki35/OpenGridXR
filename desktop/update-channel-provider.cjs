"use strict";
const { createRequire } = require("node:module");
const { GitHubProvider, computeReleaseNotes } = require("electron-updater/out/providers/GitHubProvider");
const { parseUpdateInfo } = require("electron-updater/out/providers/Provider");
// Use the same SemVer parser and Atom reader shipped by electron-updater.
const updaterRequire = createRequire(require.resolve("electron-updater/package.json"));
const semver = updaterRequire("semver");
const { parseXml, CancellationToken } = updaterRequire("builder-util-runtime");

function defaultUpdateChannel(version) {
  return semver.prerelease(version)?.[0] === "beta" ? "beta" : "stable";
}

function verifyManifest(info, tag, beta) {
  const version = semver.valid(tag);
  if (!version || semver.valid(info.version) !== version ||
      (semver.prerelease(version) && (!beta || semver.prerelease(version)[0] !== "beta"))) {
    throw new Error(`Update metadata for ${tag} does not match the selected release channel.`);
  }
  return { ...info, tag };
}

class ChannelGitHubProvider extends GitHubProvider {
  async getLatestVersion() {
    if (!this.updater.allowPrerelease) {
      const release = await super.getLatestVersion();
      return verifyManifest(release, release.tag, false);
    }
    // GitHub orders Atom entries by publication date. A stable maintenance
    // release may be published after a higher-version beta (or vice versa).
    const token = new CancellationToken();
    const feedUrl = new URL(`${this.basePath}.atom`, this.baseUrl);
    const feed = parseXml(await this.httpRequest(feedUrl, { Accept: "application/atom+xml" }, token));
    let selected;
    for (const entry of feed.getElements("entry")) {
      const tag = /\/tag\/(v?[^/]+)$/.exec(entry.element("link").attribute("href"))?.[1];
      const version = tag && semver.valid(tag);
      if (!version) continue;
      const prerelease = semver.prerelease(version);
      if (prerelease && prerelease[0] !== "beta") continue;
      if (!selected || semver.gt(version, selected.version)) selected = { tag, version, entry, beta: !!prerelease };
    }
    if (!selected) throw new Error("No stable or beta release was found on GitHub.");
    const channel = selected.beta ? "beta" : "latest";
    const file = `${this.getCustomChannelName(channel)}.yml`;
    const url = new URL(this.getBaseDownloadPath(selected.tag, file), this.baseUrl);
    const info = parseUpdateInfo(await this.httpRequest(url, null, token), file, url.href);
    info.releaseName ??= selected.entry.elementValueOrEmpty("title");
    info.releaseNotes ??= computeReleaseNotes(this.updater.currentVersion, this.updater.fullChangelog, feed, selected.entry);
    return verifyManifest(info, selected.tag, true);
  }
}
module.exports = { ChannelGitHubProvider, defaultUpdateChannel };

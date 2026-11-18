import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { NsisUpdater } = require('electron-updater/out/NsisUpdater');
const { GitHubProvider } = require('electron-updater/out/providers/GitHubProvider');
const { createUpdateController } = require('../desktop/update-controller.cjs');

async function releases(t, installed, stored = {}) {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-channel-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  const updater = new NsisUpdater(undefined, {
    version: installed, name: 'OpenGrid XR test', isPackaged: true, userDataPath: folder,
    whenReady: async () => {}, onQuit: () => {},
  });
  updater.logger = null;
  const fixtures = { stable: '1.0.2', entries: ['1.2.0-alpha.1', '1.0.2', '1.1.0-beta', '1.0.1'], requests: [], manifests: {} };
  for (const version of ['1.0.1','1.0.2','1.1.0-beta','1.1.0']) {
    const channel = version.includes('-') ? 'beta' : 'latest';
    fixtures.manifests[`v${version}/${channel}.yml`] =
      `version: ${version}\nfiles:\n  - url: OpenGridXR-Setup-${version}.exe\n    sha512: sample\n`;
  }
  updater.httpExecutor = { async request(options) {
    assert.equal(options.hostname, 'github.com');
    const base = '/DSuzuki35/OpenGridXR/releases';
    fixtures.requests.push(options.path);
    if (options.path === base+'.atom') return '<feed>'+fixtures.entries.map(v =>
      `<entry><title>${v}</title><link href="https://github.com/DSuzuki35/OpenGridXR/releases/tag/v${v}"/><content>notes</content></entry>`).join('')+'</feed>';
    if (options.path === base+'/latest') return JSON.stringify({ tag_name: 'v'+fixtures.stable });
    const file = options.path.slice((base+'/download/').length);
    assert.ok(options.path.startsWith(base+'/download/') && file in fixtures.manifests, 'Unavailable fixture: '+options.path);
    return fixtures.manifests[file];
  }};
  const controller = createUpdateController({ updater, currentVersion: installed, enabled: true,
    readPreferences: async () => stored, writePreferences: async next => { stored = next; },
    ensureCanInstall: async () => assert.fail('installer must not run'),
  });
  t.after(() => controller.close());
  assert.ok((await updater.clientPromise) instanceof GitHubProvider);
  return { fixtures, controller, updater, stored: () => stored };
}

test('stable 1.0.1 can opt into beta despite a later stable maintenance publication, then graduate to stable 1.1.0', async t => {
  const { controller, fixtures, updater, stored } = await releases(t, '1.0.1');
  assert.equal((await controller.check()).version, '1.0.2');
  assert.equal(controller.status().channel, 'stable');
  assert.ok(fixtures.requests.some(url => url.endsWith('/v1.0.2/latest.yml')));
  assert.ok(!fixtures.requests.some(url => url.endsWith('/beta.yml')));
  assert.equal((await controller.setChannel('beta')).version,'1.1.0-beta');
  assert.equal(stored().channel,'beta');
  assert.equal(updater.channel,'beta');
  assert.equal(updater.allowDowngrade,false);
  assert.ok(fixtures.requests.some(url => url.endsWith('/v1.1.0-beta/beta.yml')));
  const {info, provider} = updater.updateInfoAndProvider;
  assert.equal(provider.resolveFiles(info)[0].url.pathname,'/DSuzuki35/OpenGridXR/releases/download/v1.1.0-beta/OpenGridXR-Setup-1.1.0-beta.exe');
  fixtures.entries.push('1.1.0');
  assert.equal((await controller.check(true)).version,'1.1.0');
  assert.ok(fixtures.requests.at(-1).endsWith('/v1.1.0/latest.yml'));
});

test('beta installation defaults to beta; opt out immediately without downgrading, and retain this choice after restart', async t => {
  const { controller, updater, fixtures, stored } = await releases(t,'1.1.0-beta');
  assert.equal((await controller.check()).channel,'beta');
  assert.equal(controller.status().phase,'current');
  assert.equal((await controller.setChannel('stable')).phase,'current');
  assert.equal(updater.channel,'latest');
  assert.equal(updater.allowPrerelease,false);
  assert.equal(updater.allowDowngrade,false);
  assert.equal(controller.status().version,null);
  assert.ok(fixtures.requests.at(-1).endsWith('/v1.0.2/latest.yml'));
  const afterRestart=await releases(t,'1.1.0-beta',stored());
  assert.equal((await afterRestart.controller.check()).channel,'stable');
});

test('a mismatched manifest cannot offer a beta on stable or a different version on beta', async t => {
  const { controller, fixtures }=await releases(t,'1.0.1');
  fixtures.manifests['v1.0.2/latest.yml']=fixtures.manifests['v1.1.0-beta/beta.yml'];
  assert.equal((await controller.check()).phase,'error');
  fixtures.manifests['v1.1.0-beta/beta.yml']=fixtures.manifests['v1.1.0/latest.yml'];
  assert.equal((await controller.setChannel('beta')).phase,'error');
});

// racedrive.js - page half of the scripted walk to a race (see racedrive.sh, which starts the game and calls this).
// usage: [HYB=<s>] [PROF=<chromium profile dir>] node racedrive.js <url> <total-seconds> <shot-prefix>
// HYB=<s>: host-injected keys (BLUR_KEYS) already do title + Skip; this script just waits <s> seconds, then presses a REAL browser Enter every 8 s.
//   (Host-injected Enters do NOT dismiss the 'Some features will be unavailable' WARNING popup; browser key events do.)
// without HYB: the page also presses title Enter / Down / Enter itself (timing fragile: the Online Account dialog may not be up yet).
// prints a stats line every 8 s ('S {draws, fps, wireMB, js ms/frame, cache summary}') and saves screenshots <prefix>.cNNN.png / .final.png.
const puppeteer = require('puppeteer-core');
const fs = require('fs');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const browserPath = () => {
  if (process.env.CHROMIUM) return process.env.CHROMIUM;
  const candidates = process.platform === 'darwin'
    ? ['/Applications/Chromium.app/Contents/MacOS/Chromium', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome']
    : ['/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/google-chrome', '/usr/bin/microsoft-edge'];
  const found = candidates.find((path) => fs.existsSync(path));
  if (!found) throw new Error('No Chromium browser found; set CHROMIUM=/path/to/browser');
  return found;
};

const browserArgs = () => {
  const args = [
    '--window-size=1320,900',
    '--no-first-run',
    '--disable-default-apps',
    '--disable-background-networking',
    '--disable-component-update',
    '--disable-sync',
  ];
  if (process.platform === 'darwin') args.push('--use-angle=metal');
  if (process.platform === 'linux') args.push('--enable-gpu', `--use-angle=${process.env.BLUR_ANGLE || 'vulkan'}`);
  if (process.env.BLUR_SWIFTSHADER === '1') args.push('--enable-unsafe-swiftshader');
  return args;
};

(async () => {
  const [url, secs, pre] = process.argv.slice(2);
  let b;
  try {
    b = await puppeteer.launch({ executablePath: browserPath(), headless: 'new', userDataDir: process.env.PROF || '/tmp/blur-trackA/prof_drv', args: browserArgs(), defaultViewport: { width: 1320, height: 900 } });
    const p = await b.newPage(); await p.goto(url); await p.bringToFront();
    const renderer = await p.evaluate(() => {
      const canvas = document.createElement('canvas');
      const gl = canvas.getContext('webgl2');
      const debug = gl && gl.getExtension('WEBGL_debug_renderer_info');
      return debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : 'unknown';
    });
    console.log(`GPU ${renderer}`);
    if (/SwiftShader/i.test(renderer) && process.env.BLUR_SWIFTSHADER !== '1') throw new Error(`software renderer refused: ${renderer}`);
    const key = async (k, hold = 200) => { await p.keyboard.down(k); await sleep(hold); await p.keyboard.up(k); };
    let maxDraws = 0;
    const stat = async () => {
      const value = await p.evaluate(() => { const s = document.getElementById('stats').textContent, m = /draws\/frame (\d+)/.exec(s), skipped = /skipped (\d+)/.exec(s), f = document.getElementById('fps').textContent; return { draws: m ? +m[1] : 0, skipped: skipped ? +skipped[1] : 0, fps: f, wireMB: +(window.__wire.bytes / 1048576).toFixed(0), js: (/JS ([\d.]+) ms/.exec(s) || [])[1], hc: window.__hc && window.__hc.summary() }; });
      maxDraws = Math.max(maxDraws, value.draws);
      return { ...value, maxDraws };
    };
    const t0 = Date.now(), el = () => ((Date.now() - t0) / 1000) | 0; let n = 0;
    const shot = async (tag) => { await p.screenshot({ path: `${pre}.${tag}.png` }); console.log(el(), tag, JSON.stringify(await stat())); };
    if (process.env.HYB) { await sleep(+process.env.HYB * 1000); await shot('a_hyb'); } else {
    await sleep(25000); await key('Enter'); await sleep(7000); await shot('a_afterTitle');
    await key('ArrowDown'); await sleep(1500); await key('Enter'); await sleep(5000); await shot('b_afterSkip'); }
    while (el() < +secs) { await key('Enter'); await sleep(8000); console.log(el(), 'S', JSON.stringify(await stat())); if (++n % 3 === 0) await shot('c' + String(el()).padStart(3, '0')); }
    await shot('final');
    const final = await stat();
    if (maxDraws < 500 || final.skipped > 1000) throw new Error(`race not reached or invalid render state: ${JSON.stringify(final)}`);
  } finally {
    if (b) await b.close();
  }
})();

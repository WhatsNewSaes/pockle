#pragma once
// The status screen stays on the device's hotspot while the station joins Wi-Fi.
static const char CONNECTION_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pockle connection</title>
<style>body{background:#fffbf0;color:#1f1a4d;font:17px/1.5 ui-rounded,"SF Pro Rounded",system-ui,-apple-system,"Segoe UI",sans-serif;max-width:440px;margin:0 auto;padding:28px 20px 40px}.mark{display:flex;align-items:center;gap:10px;font-weight:800;font-size:26px;letter-spacing:-.02em}.mark svg{width:30px;height:30px}h1{font-size:32px;line-height:1.1;font-weight:800;letter-spacing:-.02em;margin:26px 0 10px}h2{font-size:26px;line-height:1.15;font-weight:800;letter-spacing:-.02em;margin:26px 0 8px}p{margin:10px 0}.card{background:#d6e9fb;border-radius:28px;padding:20px;margin:22px 0}label{display:block;font-weight:700;margin:14px 0 6px}input[type=text],input[type=password],select{box-sizing:border-box;width:100%;font:inherit;padding:14px 18px;border:2px solid #1f1a4d;border-radius:18px;background:#fff;color:inherit;margin:0}select{appearance:none;-webkit-appearance:none;padding-right:44px;background:#fff url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 20 20'%3E%3Cpath d='M5 8l5 5 5-5' fill='none' stroke='%231f1a4d' stroke-width='2.5' stroke-linecap='round'/%3E%3C/svg%3E") no-repeat right 14px center/20px}button,a.btn{display:block;box-sizing:border-box;width:100%;font:inherit;font-weight:700;font-size:18px;padding:16px;margin:20px 0 6px;border:0;border-radius:999px;background:#1f1a4d;color:#fffbf0;text-align:center;text-decoration:none}button:disabled{opacity:.4}a.btn.quiet{background:#fff;color:#1f1a4d;border:2px solid #1f1a4d}.show{display:flex;align-items:center;gap:8px;margin:10px 0 0;font-size:15px;font-weight:600}.show input{margin:0;width:20px;height:20px}details{margin:18px 0}summary{cursor:pointer;font-weight:700;padding:8px 0}small{display:block;margin:6px 0 2px;opacity:.75;font-size:14px}.note{font-size:15px;opacity:.85}a{color:inherit}[hidden]{display:none}</style></head>
<body><div class="mark"><svg viewBox="0 0 64 64" aria-hidden="true"><path d="M8 10h48v26c0 13-11 22-24 22S8 49 8 36z" fill="#ff6b1c"/><path d="M15 17h34v19c0 9-8 15-17 15s-17-6-17-15z" fill="none" stroke="#1f1a4d" stroke-width="3" stroke-dasharray="5 4" stroke-linecap="round"/></svg>Pockle</div><div role="status" aria-live="polite"><h1 id="title">Connecting...</h1><p id="message">Pockle is joining your home Wi-Fi. This usually takes a few seconds.</p></div>
<div id="success" hidden><p>Tap Finish and Pockle will open its home screen.</p><button id="done">Finish setup</button></div>
<div id="retry" hidden><a class="btn quiet" href="/">Check Wi-Fi details</a><button id="check">Check again</button></div>
<script>
const el = id => document.getElementById(id);
let started = Date.now();
let terminal = false;
function showFailure(title, message) {
  terminal = true;
  el('title').textContent = title;
  el('message').textContent = message;
  el('retry').hidden = false;
}
async function checkConnection() {
  if (terminal) return;
  try {
    const response = await fetch('/status', {cache:'no-store', signal:AbortSignal.timeout(4000)});
    if (!response.ok) throw new Error('status unavailable');
    const status = await response.json();
    if (status.state === 'connected') {
      terminal = true;
      el('title').textContent = 'Connected!';
      el('message').textContent = status.clockReady ? 'Pockle is online and ready.' : 'Wi-Fi is connected. Pockle is setting its clock.';
      el('success').hidden = false;
      el('retry').hidden = true;
      return;
    }
    if (status.state === 'failed') {
      showFailure('Could not connect', status.message || 'Check the Wi-Fi name and password. Use a 2.4 GHz network.');
      return;
    }
  } catch (error) {
    // The phone can briefly lose the hotspot when the board changes Wi-Fi channel.
  }
  if (Date.now() - started >= 45000) {
    showFailure('Check Pockle', 'The phone lost touch with Pockle. If its screen says CONNECTED, setup worked and you can close this page. Otherwise rejoin the Pockle network and check your Wi-Fi details.');
    return;
  }
  setTimeout(checkConnection, 1500);
}
el('check').onclick = () => { terminal=false; started=Date.now(); el('retry').hidden=true; el('title').textContent='Checking connection...'; checkConnection(); };
el('done').onclick = async () => {
  el('done').disabled=true;
  el('success').hidden=true;
  el('title').textContent='All set!';
  el('message').textContent='Pockle is opening its home screen. Your phone will return to your own Wi-Fi on its own. You can close this page.';
  try { await fetch('/done', {method:'POST', signal:AbortSignal.timeout(3000)}); } catch(error) {}
};
checkConnection();
</script></body></html>)HTML";

// The setup form. The board fills the slots: {{NETS}} with the networks it found (strongest first),
// {{ZONES}} with the timezone options, {{KEYNOTE}} and {{LOC}} with what is already saved.
// The phone's own timezone picks the zone, the networks found sit in one dropdown (the one the
// board is already on chosen, else "Choose your Wi-Fi..."), an open network hides the password,
// so on a first run the network and its password are all there is to do; CONNECT stays off
// until both are there (or the network needs no password).
static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pockle Wi-Fi</title>
<style>body{background:#fffbf0;color:#1f1a4d;font:17px/1.5 ui-rounded,"SF Pro Rounded",system-ui,-apple-system,"Segoe UI",sans-serif;max-width:440px;margin:0 auto;padding:28px 20px 40px}.mark{display:flex;align-items:center;gap:10px;font-weight:800;font-size:26px;letter-spacing:-.02em}.mark svg{width:30px;height:30px}h1{font-size:32px;line-height:1.1;font-weight:800;letter-spacing:-.02em;margin:26px 0 10px}h2{font-size:26px;line-height:1.15;font-weight:800;letter-spacing:-.02em;margin:26px 0 8px}p{margin:10px 0}.card{background:#d6e9fb;border-radius:28px;padding:20px;margin:22px 0}label{display:block;font-weight:700;margin:14px 0 6px}input[type=text],input[type=password],select{box-sizing:border-box;width:100%;font:inherit;padding:14px 18px;border:2px solid #1f1a4d;border-radius:18px;background:#fff;color:inherit;margin:0}select{appearance:none;-webkit-appearance:none;padding-right:44px;background:#fff url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 20 20'%3E%3Cpath d='M5 8l5 5 5-5' fill='none' stroke='%231f1a4d' stroke-width='2.5' stroke-linecap='round'/%3E%3C/svg%3E") no-repeat right 14px center/20px}button,a.btn{display:block;box-sizing:border-box;width:100%;font:inherit;font-weight:700;font-size:18px;padding:16px;margin:20px 0 6px;border:0;border-radius:999px;background:#1f1a4d;color:#fffbf0;text-align:center;text-decoration:none}button:disabled{opacity:.4}a.btn.quiet{background:#fff;color:#1f1a4d;border:2px solid #1f1a4d}.show{display:flex;align-items:center;gap:8px;margin:10px 0 0;font-size:15px;font-weight:600}.show input{margin:0;width:20px;height:20px}details{margin:18px 0}summary{cursor:pointer;font-weight:700;padding:8px 0}small{display:block;margin:6px 0 2px;opacity:.75;font-size:14px}.note{font-size:15px;opacity:.85}a{color:inherit}[hidden]{display:none}</style></head>
<body><div class="mark"><svg viewBox="0 0 64 64" aria-hidden="true"><path d="M8 10h48v26c0 13-11 22-24 22S8 49 8 36z" fill="#ff6b1c"/><path d="M15 17h34v19c0 9-8 15-17 15s-17-6-17-15z" fill="none" stroke="#1f1a4d" stroke-width="3" stroke-dasharray="5 4" stroke-linecap="round"/></svg>Pockle</div><h1>Let&rsquo;s get Pockle online.</h1><p>Choose your home Wi-Fi and type its password.</p>
<form action="/save" method="post"><input type="hidden" name="epoch" id="epoch">
<div class="card">
<label for="pick">Wi-Fi network</label><select name="pick" id="pick"><option value="" data-none="1" id="picknone" disabled selected hidden>Choose your Wi-Fi...</option>{{NETS}}<option value="" id="pickother">Other network...</option></select>
<div id="other" hidden><label for="ssid">Network name</label><input type="text" name="ssid" id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" placeholder="Wi-Fi name"></div>
<div id="pw"><label for="password">Password</label><input type="password" name="password" id="password" maxlength="63" autocomplete="off" placeholder="Wi-Fi password"><label class="show"><input type="checkbox" id="show">Show password</label></div>
<button id="connect" disabled>Connect Pockle</button>
</div>
<details><summary>Advanced options</summary>
<label for="zone">Timezone</label><select name="zone" id="zone">{{ZONES}}</select><small id="zonenote">Pick the zone Pockle lives in.</small>
<label>Voice key (OpenRouter)</label><input type="text" name="key" maxlength="128" autocomplete="off" placeholder="sk-or-v1-..."><small>{{KEYNOTE}}</small>
<label>Weather location</label><input type="text" name="loc" maxlength="40" value="{{LOC}}" placeholder="ZIP or city"><small>Leave blank and Pockle works out its location once it is online.</small>
</details>
</form>
<p class="note">Your password is saved only on Pockle. Use a 2.4 GHz network. <a href="/scan">Scan again</a> if your network is not listed.</p>
<script>
const el = id => document.getElementById(id);
const zones = {'America/New_York':0,'America/Detroit':0,'America/Chicago':1,'America/Menominee':1,'America/Denver':2,'America/Boise':2,'America/Phoenix':2,'America/Los_Angeles':3};
function zoneIndex(tz) {
  if (!tz) return -1;
  if (tz in zones) return zones[tz];
  if (/^America\/(Kentucky|Indiana)\//.test(tz)) return tz === 'America/Indiana/Knox' || tz === 'America/Indiana/Tell_City' ? 1 : 0;
  if (/^America\/North_Dakota\//.test(tz)) return 1;
  return -1;
}
try {
  const zone = el('zone');
  const i = zoneIndex(Intl.DateTimeFormat().resolvedOptions().timeZone);
  if (i >= 0 && zone.dataset.saved !== '1') { zone.value = String(i); el('zonenote').textContent = 'Detected from your phone.'; }
} catch (error) {}
function picked() { const pick = el('pick'); return pick.options[pick.selectedIndex] || null; }
function ready() {
  const opt = picked(), none = !opt || opt.dataset.none === '1', other = !none && opt.value === '', open = !none && !other && opt.dataset.open === '1';
  const named = !none && (!other || el('ssid').value.trim().length > 0);
  el('connect').disabled = !(named && (open || el('password').value.length > 0));
}
function pickChanged() {
  const opt = picked(), other = !!opt && opt.dataset.none !== '1' && opt.value === '';
  el('other').hidden = !other;
  el('ssid').required = other;
  const open = !!opt && !other && opt.dataset.none !== '1' && opt.dataset.open === '1';
  el('pw').hidden = open;
  if (open) el('password').value = '';
  ready();
}
el('pick').onchange = pickChanged;
el('ssid').oninput = ready;
el('password').oninput = ready;
pickChanged();
el('show').onchange = () => { el('password').type = el('show').checked ? 'text' : 'password'; };
el('epoch').value = Math.floor(Date.now() / 1000);
</script></body></html>)HTML";

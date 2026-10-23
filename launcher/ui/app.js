const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
let CFG = null, PORT = null, HASH_TIMER = null;

const ACTIONS = [
  ["up","Up"],["down","Down"],["left","Left"],["right","Right"],
  ["punch","Punch"],["kick","Kick"],["guard","Guard"],["dodge","Dodge"],
  ["special","Special"],["taunt","Taunt"],["pause","Pause"],["confirm","Confirm"],
];

/* ------------------------------------------------ api */
async function api(path, body) {
  const opt = body ? {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)} : {};
  const r = await fetch(path, opt);
  const txt = await r.text();
  let data; try { data = JSON.parse(txt); } catch { data = {ok:false, error:txt.slice(0,200)}; }
  return {status:r.status, data};
}

/* ------------------------------------------------ boot */
async function boot() {
  const {data} = await api("/api/state");
  CFG = data.config; PORT = data.port;
  paintConfig(); paintPort(); paintIso(data.iso); paintHash(data.hash);
  if (data.iso) setDiscPill(data.iso.ok);
  bindEvents(); pollGamepad();
  if (data.iso && data.iso.ok) runHashStatus();
}

/* ------------------------------------------------ tabs */
$$(".tab").forEach(t => t.onclick = () => {
  $$(".tab").forEach(x => x.classList.remove("active"));
  $$(".panel").forEach(x => x.classList.remove("active"));
  t.classList.add("active");
  $("#panel-" + t.dataset.tab).classList.add("active");
});

/* ------------------------------------------------ paint */
function paintConfig() {
  $("#isoPath").value = CFG.iso_path || "";
  $("#exePath").value = CFG.exe_path || "";
  $("#resolution").value = CFG.resolution;
  if (![...$("#resolution").options].some(o => o.value === CFG.resolution)) {
    $("#resolution").value = "custom";
  }
  syncCustomRes();
  $$("#segRenderer button").forEach(b => b.classList.toggle("on", b.dataset.v === CFG.renderer));
  $("#window_mode").value = CFG.window_mode;
  $("#internal_scale").value = CFG.internal_scale;
  $("#scaleVal").textContent = Number(CFG.internal_scale).toFixed(2) + "×";
  $("#vsync").checked = !!CFG.vsync;
  $("#widescreen").checked = !!CFG.widescreen;
  $("#frame_skip").checked = !!CFG.frame_skip;
  $("#msaa").value = String(CFG.msaa);
  $("#anisotropy").value = String(CFG.anisotropy);
  $("#texture_filter").value = CFG.texture_filter;
  $("#fps_cap").value = String(CFG.fps_cap);
  $("#volume_master").value = CFG.volume_master; $("#vMaster").textContent = CFG.volume_master;
  $("#volume_bgm").value = CFG.volume_bgm; $("#vBgm").textContent = CFG.volume_bgm;
  $("#volume_sfx").value = CFG.volume_sfx; $("#vSfx").textContent = CFG.volume_sfx;
  $("#audio_backend").value = CFG.audio_backend;
  $("#language").value = CFG.language;
  $("#gpEnabled").checked = !!CFG.gamepad.enabled;
  $("#gpVib").checked = !!CFG.gamepad.vibration;
  $("#gpDeadzone").value = CFG.gamepad.deadzone;
  $("#dzVal").textContent = Number(CFG.gamepad.deadzone).toFixed(2);
  paintBinds(); paintSummary();
}

function paintPort() {
  if (!PORT) return;
  $("#phaseLabel").textContent = "engine phase " + PORT.done + "/" + PORT.total;
  $("#phasePct").textContent = PORT.percent + "%";
  $("#phaseBar").style.width = PORT.percent + "%";
  $("#binList").innerHTML = PORT.binaries.length
    ? PORT.binaries.map(b => `<li>${b.path} · ${(b.size/1e6).toFixed(1)} MB</li>`).join("")
    : `<li>belum ada binary hasil build</li>`;
  const ready = PORT.engine_ready;
  $("#playNotice").textContent = ready
    ? "Engine binary terdeteksi — Play akan menjalankan port native."
    : `Engine belum playable. Fase berikutnya: ${PORT.next || "—"}`;
  paintSummary();
}

function paintIso(iso) {
  if (!iso) { ["dId","dRegion","dVer","dVol","dSize","dBoot"].forEach(i => $("#"+i).textContent = "—"); return; }
  if (!iso.ok) {
    $("#dId").textContent = "INVALID"; $("#dId").className = "error";
    $("#dRegion").textContent = iso.error || "—";
    return;
  }
  $("#dId").className = ""; 
  $("#dId").textContent = iso.disc_id;
  $("#dRegion").textContent = iso.region;
  $("#dVer").textContent = iso.version;
  $("#dVol").textContent = iso.volume_id;
  $("#dSize").textContent = iso.size_h;
  $("#dBoot").textContent = iso.boot_file;
  setDiscPill(true);
}

function setDiscPill(ok) {
  $("#discPill").className = "pill " + (ok ? "on" : "off");
  $("#discText").textContent = ok ? "disc mounted" : "no disc";
}

function paintHash(h) {
  if (!h) return;
  $("#hashBar").style.width = (h.pct || 0) + "%";
  $("#hashVal").textContent = h.value || (h.status === "running" ? `hashing… ${h.pct}%` : "—");
  $("#hashVal").className = h.value ? "ok" : "";
}

function paintSummary() {
  if (!CFG || !PORT) return;
  const isoName = CFG.iso_path ? CFG.iso_path.split(/[\\/]/).pop() : "belum dipilih";
  const res = CFG.resolution === "custom" ? `${$("#resW").value}×${$("#resH").value}` : CFG.resolution.replace("x","×");
  $("#summary").innerHTML = [
    ["Disc", isoName],
    ["Renderer", CFG.renderer.toUpperCase()],
    ["Resolution", res + (Number(CFG.internal_scale) > 1 ? ` @ ${CFG.internal_scale}×` : "")],
    ["Display", CFG.window_mode + (CFG.vsync ? " · vsync" : "") + (CFG.fps_cap > 0 ? ` · ${CFG.fps_cap} fps` : " · uncapped")],
    ["Engine", PORT.engine_ready ? "native binary ready" : `phase ${PORT.done}/${PORT.total}`],
  ].map(([k,v]) => `<li>${k}: <b>${v}</b></li>`).join("");
}

/* ------------------------------------------------ binds */
function paintBinds() {
  $("#keybindList").innerHTML = ACTIONS.map(([k,label]) =>
    `<div class="bind" data-act="${k}"><small>${label}</small><b>${prettyKey(CFG.keybinds[k])}</b></div>`).join("");
  const padNames = ["A / Cross","B / Circle","X / Square","Y / Triangle","LB","RB","LT","RT","Back","Start","LS","RS"];
  $("#padbindList").innerHTML = ACTIONS.map(([act,label]) => {
    const idx = CFG.gamepad.map[act];
    return `<div class="bind" data-pad="${act}"><small>${label}</small><b>${idx === undefined ? "—" : (padNames[idx] || "btn "+idx)}</b></div>`;
  }).join("");
}

function prettyKey(code) {
  if (!code) return "—";
  return code.replace(/^Key/, "").replace(/^Digit/, "").replace("Arrow", "").toUpperCase();
}

/* ------------------------------------------------ events */
function bindEvents() {
  $("#btnBrowseIso").onclick = async () => {
    const {data} = await api("/api/browse", {kind:"iso"});
    if (data.path) { $("#isoPath").value = data.path; CFG.iso_path = data.path; checkIso(); }
  };
  $("#btnBrowseExe").onclick = async () => {
    const {data} = await api("/api/browse", {kind:"exe"});
    if (data.path) { $("#exePath").value = data.path; CFG.exe_path = data.path; save(); }
  };
  $("#btnCheckIso").onclick = checkIso;
  $("#isoPath").onchange = checkIso;

  $("#btnHash").onclick = async () => {
    const {data} = await api("/api/hash", {});
    if (!data.ok) { $("#hashVal").textContent = data.error; return; }
    runHashStatus();
  };

  $$("#segRenderer button").forEach(b => b.onclick = () => {
    $$("#segRenderer button").forEach(x => x.classList.remove("on"));
    b.classList.add("on"); CFG.renderer = b.dataset.v; save(); paintSummary();
  });

  $("#resolution").onchange = () => {
    syncCustomRes();
    if ($("#resolution").value === "custom") {
      CFG.resolution = `${$("#resW").value}x${$("#resH").value}`;
    } else CFG.resolution = $("#resolution").value;
    save(); paintSummary();
  };
  ["resW","resH"].forEach(id => $("#"+id).onchange = () => {
    CFG.resolution = `${$("#resW").value}x${$("#resH").value}`; save(); paintSummary();
  });
  $("#window_mode").onchange = e => { CFG.window_mode = e.target.value; save(); paintSummary(); };
  $("#internal_scale").oninput = e => {
    CFG.internal_scale = Number(e.target.value);
    $("#scaleVal").textContent = CFG.internal_scale.toFixed(2) + "×"; save(); paintSummary();
  };
  ["vsync","widescreen","frame_skip"].forEach(id => $("#"+id).onchange = e => {
    CFG[id] = e.target.checked; save(); paintSummary();
  });
  $("#msaa").onchange = e => { CFG.msaa = Number(e.target.value); save(); };
  $("#anisotropy").onchange = e => { CFG.anisotropy = Number(e.target.value); save(); };
  $("#texture_filter").onchange = e => { CFG.texture_filter = e.target.value; save(); };
  $("#fps_cap").onchange = e => { CFG.fps_cap = Number(e.target.value); save(); paintSummary(); };
  $("#audio_backend").onchange = e => { CFG.audio_backend = e.target.value; save(); };
  $("#language").onchange = e => { CFG.language = e.target.value; save(); };

  [["volume_master","vMaster"],["volume_bgm","vBgm"],["volume_sfx","vSfx"]].forEach(([id,lab]) => {
    $("#"+id).oninput = e => { CFG[id] = Number(e.target.value); $("#"+lab).textContent = e.target.value; save(); };
  });

  $("#gpEnabled").onchange = e => { CFG.gamepad.enabled = e.target.checked; save(); };
  $("#gpVib").onchange = e => { CFG.gamepad.vibration = e.target.checked; save(); };
  $("#gpDeadzone").oninput = e => {
    CFG.gamepad.deadzone = Number(e.target.value);
    $("#dzVal").textContent = CFG.gamepad.deadzone.toFixed(2); save();
  };

  $("#btnResetKeys").onclick = async () => {
    const {data} = await api("/api/save", {});
    CFG.keybinds = {up:"KeyW",down:"KeyS",left:"KeyA",right:"KeyD",punch:"KeyJ",kick:"KeyK",
      guard:"KeyL",dodge:"Space",special:"KeyI",taunt:"KeyU",pause:"Escape",confirm:"Enter"};
    await save(); paintBinds();
  };

  $("#btnPlay").onclick = async () => {
    const box = $("#console");
    box.textContent = "launching…";
    const {data} = await api("/api/play", {});
    box.textContent = data.ok
      ? `running pid ${data.pid}\n${data.argv.join(" ")}\nlog: ${data.log}`
      : `BLOCKED [${data.stage}] ${data.error}\n${data.hint || ""}\nnext: ${data.next || "—"}`;
  };
  $("#btnDiag").onclick = async () => {
    const box = $("#console");
    box.textContent = "running crt0_test (5M inst cap)…";
    const {data} = await api("/api/diag", {});
    box.textContent = (data.tail || []).join("\n") || data.error || "no output";
  };
  $("#btnSave").onclick = async () => { await save(); $("#console").textContent = "config saved → " + ($("#cfgPath").textContent || "~/.config/godhand-launcher/config.json"); };

  // keybind capture
  document.addEventListener("click", e => {
    const el = e.target.closest(".bind[data-act]");
    if (!el) return;
    $$(".bind").forEach(b => b.classList.remove("rec"));
    el.classList.add("rec");
    el.querySelector("b").textContent = "press…";
    const handler = ev => {
      ev.preventDefault();
      CFG.keybinds[el.dataset.act] = ev.code;
      el.querySelector("b").textContent = prettyKey(ev.code);
      el.classList.remove("rec");
      document.removeEventListener("keydown", handler, true);
      save(); paintBinds();
    };
    document.addEventListener("keydown", handler, true);
  });
}

function syncCustomRes() {
  const custom = $("#resolution").value === "custom" || !$("#resolution").value;
  $("#customResWrap").classList.toggle("hide", !custom);
  if (custom && CFG.resolution.includes("x")) {
    const [w,h] = CFG.resolution.split("x");
    $("#resW").value = w; $("#resH").value = h;
  }
}

async function checkIso() {
  const path = $("#isoPath").value.trim();
  const {data} = await api("/api/iso", {path});
  paintIso(data);
  setDiscPill(!!data.ok);
  if (data.ok) { CFG.iso_path = path; await save(); runHashStatus(); }
  else {
    $("#dId").textContent = "INVALID"; $("#dId").className = "error";
    $("#dRegion").textContent = data.error; setDiscPill(false);
  }
}

let saveTimer = null;
function save() {
  clearTimeout(saveTimer);
  return new Promise(res => {
    saveTimer = setTimeout(async () => {
      await api("/api/save", CFG);
      res();
    }, 120);
  });
}

async function runHashStatus() {
  clearInterval(HASH_TIMER);
  const tick = async () => {
    const {data} = await api("/api/hash");
    paintHash(data);
    if (data.status !== "running") clearInterval(HASH_TIMER);
  };
  tick(); HASH_TIMER = setInterval(tick, 900);
}

/* ------------------------------------------------ gamepad */
let padIndex = null, capturing = null;

function pollGamepad() {
  const gp = navigator.getGamepads ? [...navigator.getGamepads()].find(p => p && p.connected) : null;
  const pill = $("#padStatus");
  if (gp) {
    padIndex = gp.index;
    pill.className = "pill small on";
    pill.lastElementChild.textContent = `${gp.id.slice(0, 28)} · ${gp.buttons.length} btn`;
    if (capturing) {
      const pressed = gp.buttons.findIndex(b => b.pressed);
      if (pressed >= 0) {
        const act = capturing;
        CFG.gamepad.map[act] = pressed;
        capturing = null;
        paintBinds(); save();
      }
    }
  } else {
    padIndex = null;
    pill.className = "pill small";
    pill.lastElementChild.textContent = "tidak terdeteksi";
  }
  requestAnimationFrame(pollGamepad);
}

document.addEventListener("click", e => {
  const el = e.target.closest(".bind[data-pad]");
  if (!el) return;
  capturing = el.dataset.pad;
  $$(".bind").forEach(b => b.classList.remove("rec"));
  el.classList.add("rec");
  el.querySelector("b").textContent = "press…";
  setTimeout(() => { if (capturing === el.dataset.pad) { capturing = null; el.classList.remove("rec"); paintBinds(); } }, 6000);
});

window.addEventListener("gamepadconnected", () => {});
boot();

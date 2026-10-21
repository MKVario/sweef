// Sweef-App: verbindet die Web-Oberflaeche mit dem Android-Teil der App.
// Stellt navigator.bluetooth nach (so wie Chrome es hat), aber die echte
// Verbindung haelt die App selbst - damit laeuft sie auch im Hintergrund.
// Zusaetzlich: Bereich "App" im Reiter "Geraet" (GPS-Tempo, Firmware-Update).
(function () {
  "use strict";
  if (!window.SweefNative) return;
  const N = window.SweefNative;

  // ---------------- Rueckrufe von Android ----------------
  let nr = 0;
  const offen = {};
  function rufen(fn, ...args) {
    return new Promise((ok, nein) => {
      const id = ++nr;
      offen[id] = { ok, nein };
      N[fn](id, ...args);
    });
  }

  const b64zuDV = s => {
    const bin = atob(s || "");
    const a = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
    return new DataView(a.buffer);
  };
  const zuB64 = v => {
    const a = v instanceof ArrayBuffer ? new Uint8Array(v)
      : new Uint8Array(v.buffer, v.byteOffset || 0, v.byteLength);
    let s = "";
    for (let i = 0; i < a.length; i++) s += String.fromCharCode(a[i]);
    return btoa(s);
  };

  // ---------------- Bluetooth nachbilden ----------------
  const kanaele = {};
  let aktuell = null;        // ausgewaehltes Geraet
  let jsVerbunden = false;   // hat die Seite seit dem letzten Abbruch verbunden?

  class Kanal extends EventTarget {
    constructor(uuid) { super(); this.uuid = uuid; this.value = null; }
    writeValueWithResponse(v) { return rufen("schreiben", this.uuid, zuB64(v)).then(() => undefined); }
    writeValue(v) { return this.writeValueWithResponse(v); }
    writeValueWithoutResponse(v) { return this.writeValueWithResponse(v); }
    readValue() { return rufen("lesen", this.uuid).then(s => (this.value = b64zuDV(s))); }
    startNotifications() { kanaele[this.uuid] = this; return rufen("melden", this.uuid).then(() => this); }
  }
  class Dienst {
    constructor(uuid) { this.uuid = uuid; }
    getCharacteristic(u) {
      u = String(u).toLowerCase();
      return rufen("hatKanal", u).then(() => kanaele[u] || new Kanal(u));
    }
  }
  class Server {
    constructor(dev) { this.device = dev; }
    get connected() { return jsVerbunden && N.istVerbunden(); }
    getPrimaryService(u) { return Promise.resolve(new Dienst(String(u).toLowerCase())); }
  }
  class Gatt {
    constructor(dev) { this.device = dev; }
    get connected() { return jsVerbunden && N.istVerbunden(); }
    connect() {
      return rufen("verbinden", this.device.id, this.device.name || "Sweef")
        .then(() => { jsVerbunden = true; return new Server(this.device); });
    }
    disconnect() { N.trennen(); }
  }
  class Geraet extends EventTarget {
    constructor(id, name) { super(); this.id = id; this.name = name; this.gatt = new Gatt(this); }
  }

  function keinGeraet(text) { const e = new Error(text || "Kein Gerät ausgewählt."); e.name = "NotFoundError"; return e; }

  // Auswahlfenster mit den gefundenen Varios
  function auswahl(liste) {
    return new Promise((ok, nein) => {
      const hg = document.createElement("div");
      hg.style.cssText = "position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:999;display:flex;align-items:center;justify-content:center;padding:16px";
      const box = document.createElement("div");
      box.style.cssText = "background:var(--surface);color:var(--fg);border:1px solid var(--line);border-radius:18px;padding:16px;width:100%;max-width:360px;font:inherit";
      box.innerHTML = "<h2 style='margin:0 0 10px;font:700 1.3rem var(--f-display)'>Vario auswählen</h2>";
      if (!liste.length) {
        box.insertAdjacentHTML("beforeend", "<p>Kein Sweef gefunden. Ist das Vario eingeschaltet und in der Nähe?</p>");
      }
      liste.forEach(d => {
        const b = document.createElement("button");
        b.className = "primary";
        b.style.cssText = "display:block;width:100%;margin:6px 0";
        b.textContent = d.name + (d.rssi ? "  (" + (d.rssi > -60 ? "nah" : d.rssi > -80 ? "mittel" : "weit") + ")" : "");
        b.onclick = () => { hg.remove(); ok(d); };
        box.appendChild(b);
      });
      const ab = document.createElement("button");
      ab.textContent = "Abbrechen";
      ab.style.cssText = "display:block;width:100%;margin-top:10px";
      ab.onclick = () => { hg.remove(); nein(keinGeraet()); };
      box.appendChild(ab);
      hg.appendChild(box);
      document.body.appendChild(hg);
    });
  }

  const bluetooth = {
    async requestDevice() {
      const jetzt = N.aktuellesGeraet();
      if (jetzt && aktuell) return aktuell;                 // App ist noch verbunden
      if (jetzt) { const d = JSON.parse(jetzt); return (aktuell = new Geraet(d.id, d.name)); }
      const st = document.getElementById("status");
      if (st) st.textContent = "Suche Sweef …";
      const liste = JSON.parse(await rufen("suchen"));
      const d = await auswahl(liste);
      if (!aktuell || aktuell.id !== d.id) aktuell = new Geraet(d.id, d.name);
      return aktuell;
    },
    getAvailability() { return Promise.resolve(true); }
  };
  try { Object.defineProperty(navigator, "bluetooth", { value: bluetooth, configurable: true }); }
  catch (e) { navigator.bluetooth = bluetooth; }

  // ---------------- Ereignisse von Android ----------------
  window.SweefBridge = {
    _fertig(id, gut, wert) {
      const p = offen[id];
      if (!p) return;
      delete offen[id];
      if (gut) p.ok(wert); else p.nein(new Error(wert || "Fehler"));
    },
    _ereignis(art, d) {
      if (art === "notify") {
        const k = kanaele[String(d.u).toLowerCase()];
        if (!k) return;
        k.value = b64zuDV(d.d);
        k.dispatchEvent(new Event("characteristicvaluechanged"));
      } else if (art === "getrennt") {
        jsVerbunden = false;
        if (aktuell) aktuell.dispatchEvent(new Event("gattserverdisconnected"));
      } else if (art === "verbunden") {
        // Die App hat im Hintergrund neu verbunden: Seite richtet sich neu ein
        if (!jsVerbunden && aktuell && typeof einrichten === "function" && !absichtlichGetrennt) {
          einrichten().catch(() => {});
        }
      } else if (art === "vordergrund") {
        if (typeof kmUhr !== "undefined" && kmUhr && N.kompassStart) N.kompassStart();   // Kompass weiterlaufen lassen
        if (Date.now() - letztePruefung > 10 * 60 * 1000) versionenPruefen();
      } else if (art === "appupdate") {
        const t = document.getElementById("hinweisText");
        if (t) t.textContent = d.text;
        if (d.ende) melden(d.text);
      } else if (art === "update") {
        updateZeigen(d);
      }
    }
  };

  // ---------------- Speichern (Sicherung, CSV) ----------------
  window.herunterladen = function (name, inhalt, typ) {
    const ok = N.speichern(name, String(inhalt), typ || "");
    melden(ok ? "Gespeichert in „Download/Sweef“" : "Speichern fehlgeschlagen");
  };

  // ---------------- Bereich "App" im Reiter Geraet ----------------
  function bereichBauen() {
    const seite = document.getElementById("seite_geraet");
    if (!seite || document.getElementById("appBereich")) return;
    const s = document.createElement("section");
    s.id = "appBereich";
    s.innerHTML = `
      <h2>Updates</h2>
      <div class="schalter" style="cursor:default">
        <span><b>Sweef-App</b><small id="appVersion"></small></span>
        <button id="appNeu" class="primary" hidden>Installieren</button>
      </div>
      <div class="schalter" style="cursor:default">
        <span><b>Vario-Firmware</b><small id="fwInfo">Mit dem Vario verbinden, dann nach Updates suchen.</small></span>
      </div>
      <div class="fortschritt" id="fwBalken" hidden><i></i></div>
      <div class="knopfreihe">
        <button id="fwSuchen">Nach Update suchen</button>
        <button class="primary" id="fwStart" hidden>Jetzt aktualisieren</button>
      </div>
      <p class="note" style="margin:0 0 4px">Das Update dauert 2–3 Minuten. Vario dabei eingeschaltet und nah am Handy lassen. Bricht es ab, einfach nochmal starten.</p>`;
    const a = document.createElement("section");
    a.id = "appEinst";
    a.innerHTML = `
      <h2>App</h2>
      <label class="schalter"><span><b>GPS-Tempo ans Vario</b><small id="appTempoInfo"></small></span><input type="checkbox" class="switch" id="appTempo"></label>
      <div class="schalter" id="appAkkuZeile" style="cursor:default"><span><b>Akku-Optimierung</b><small>Ausschalten, damit die Verbindung im Hintergrund nicht abreißt.</small></span><button id="appAkku">Ausschalten</button></div>`;
    seite.insertBefore(a, seite.firstChild);
    seite.insertBefore(s, seite.firstChild);
    const w = document.createElement("section");
    w.id = "woIstVario";
    w.hidden = true;
    w.innerHTML = `
      <h2>Wo ist mein Vario?</h2>
      <p class="hint" id="woText" style="margin-bottom:10px"></p>
      <canvas class="karte" id="woKarte" style="height:220px" role="img" aria-label="Karte: hier war das Vario zuletzt verbunden"></canvas>
      <p class="note" style="margin:6px 0 0">Orange: Vario zuletzt gesehen · Weiß: du · Karte © OpenStreetMap-Mitwirkende</p>
      <div class="knopfreihe">
        <button class="primary" id="woHin">Hinführen (Google Maps)</button>
      </div>
      <p class="note" style="margin:0 0 4px">In der Nähe: verbinden und unter „Vario suchen“ piepen lassen.</p>`;
    seite.insertBefore(w, seite.firstChild);
    document.getElementById("woHin").onclick = () => {
      const z = zuletzt();
      if (z) location.href = "https://www.google.com/maps/dir/?api=1&travelmode=walking&destination=" + z.lat + "," + z.lon;
    };
    spBereichBauen();
    setInterval(woZeigen, 5000);
    document.getElementById("tab_geraet").addEventListener("click", () => setTimeout(woZeigen, 50));
    woZeigen();

    const tempo = document.getElementById("appTempo");
    tempo.checked = N.tempoAn();
    tempo.onchange = () => N.tempoSetzen(tempo.checked);
    document.getElementById("appAkku").onclick = () => N.akkuOptimierungAus();
    document.getElementById("appVersion").textContent = N.appVersion() + " · auf dem Handy";
    document.getElementById("appNeu").onclick = () => { if (neueste && neueste.appUrl) N.appInstallieren(neueste.appUrl); };
    document.getElementById("fwSuchen").onclick = fwSuchen;
    document.getElementById("fwStart").onclick = fwStarten;
    document.getElementById("appTempo").addEventListener("change", statusZeigen);
    setInterval(statusZeigen, 1000);
    setTimeout(versionenPruefen, 3000);               // kurz nach dem Start
    setInterval(versionenPruefen, 6 * 3600 * 1000);    // und alle 6 Stunden
    statusZeigen();
  }

  // ---------------- Startplaetze aus ParaglidingEarth ----------------
  function spBereichBauen() {
    const sp = document.getElementById("startplatz");
    if (!sp || document.getElementById("pgeBox")) return;
    const d = document.createElement("details");
    d.id = "pgeBox";
    d.innerHTML = `
      <summary>Startplätze aus dem Internet</summary>
      <label class="schalter"><span><b>Vario fragt am Startplatz</b><small>Steht das Vario am Boden in der Nähe eines Startplatzes, fragt es, ob dessen Höhe übernommen werden soll (T1 = Ja, T2 = Nein).</small></span><input type="checkbox" class="switch" id="spFragen"></label>
      <p class="note" id="pgeInfo" style="margin:6px 0"></p>
      <div class="knopfreihe"><button id="pgeLaden">Startplätze im Umkreis von 150 km laden</button><button id="pgeZeigen">Liste anzeigen</button></div>
      <div id="pgeListeBox" hidden>
        <input id="pgeSuche" placeholder="Suchen, z. B. Wilderness" autocomplete="off" style="width:100%;font:500 1rem var(--f-body);padding:10px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--fg);margin:4px 0 8px">
        <ul class="splist" id="pgeListe"></ul>
        <p class="note" id="pgeListeInfo" style="margin:6px 0"></p>
      </div>
      <p class="note" style="margin:0 0 8px">Vorher laden, solange du Internet hast – danach geht es auch ohne. Daten: ParaglidingEarth (CC BY-SA). Höhen können ungenau sein: bitte beim ersten Mal prüfen.</p>`;
    const vor = document.getElementById("spVerwalten");
    sp.insertBefore(d, vor);
    const sw = document.getElementById("spFragen");
    sw.checked = N.spFragenAn();
    sw.onchange = () => N.spFragenSetzen(sw.checked);
    document.getElementById("pgeLaden").onclick = pgeLaden;
    document.getElementById("pgeZeigen").onclick = () => {
      const box = document.getElementById("pgeListeBox");
      box.hidden = !box.hidden;
      document.getElementById("pgeZeigen").textContent = box.hidden ? "Liste anzeigen" : "Liste ausblenden";
      if (!box.hidden) pgeListe();
    };
    document.getElementById("pgeSuche").oninput = () => pgeListeZeigen();
    pgeInfo();
  }
  // Liste der geladenen Startplaetze, nach Entfernung sortiert
  let pgeDaten = [], pgeOrt = null;
  function pgeListe() {
    pgeDaten = JSON.parse(N.spListe(NaN, NaN, 3000) || "[]");    // erst nach Name
    pgeListeZeigen();
    navigator.geolocation.getCurrentPosition(p => {
      pgeOrt = { lat: p.coords.latitude, lon: p.coords.longitude };
      pgeDaten = JSON.parse(N.spListe(pgeOrt.lat, pgeOrt.lon, 3000) || "[]");
      pgeListeZeigen();
    }, () => {}, { enableHighAccuracy: false, timeout: 15000, maximumAge: 300000 });
  }
  function pgeListeZeigen() {
    const ul = document.getElementById("pgeListe");
    if (!ul) return;
    const such = (document.getElementById("pgeSuche").value || "").trim().toLowerCase();
    const treffer = pgeDaten.filter(x => !such || x.name.toLowerCase().includes(such));
    const zeigen = treffer.slice(0, 60);
    const e = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
    const km = m => m < 0 ? "" : m < 1000 ? Math.round(m / 10) * 10 + " m" : (m / 1000).toFixed(m < 10000 ? 1 : 0).replace(".", ",") + " km";
    ul.innerHTML = "";
    zeigen.forEach(x => {
      const li = document.createElement("li");
      const rose = x.richtungen && typeof windRose === "function" ? windRose(x.richtungen, 42) : "";
      const wind = x.richtungen && typeof richtDeutsch === "function" ? "Start " + richtDeutsch(x.richtungen) : "";
      li.innerHTML = `<span class="richtreihe">${rose}<span>${e(x.name)} · ${Math.round(x.hoehe)} m<br><span class="klein">${[km(x.m), e(wind)].filter(Boolean).join(" · ")}</span></span></span>
        <button class="spKarte">Karte</button><button class="merken">Speichern</button>`;
      li.querySelector(".spKarte").onclick = () => { location.href = "https://www.google.com/maps/search/?api=1&query=" + x.lat + "," + x.lon; };
      li.querySelector(".merken").onclick = () => {
        if (typeof startplaetze === "undefined") return;
        const neu = { id: "sp" + Date.now().toString(36), name: x.name.slice(0, 30), hoehe: Math.round(x.hoehe), lat: x.lat, lon: x.lon, richtung: x.richtungen || "" };
        startplaetze.push(neu);
        if (speicherSchreiben(SP_KEY, startplaetze)) melden("„" + neu.name + "“ bei deinen Startplätzen gespeichert");
        spZeigen(neu.id);
        spFormular(neu);                             // gleich zum Pruefen/Korrigieren oeffnen
        document.getElementById("spNeu").scrollIntoView({ behavior: "smooth", block: "center" });
      };
      ul.appendChild(li);
    });
    document.getElementById("pgeListeInfo").textContent = !pgeDaten.length ? "Noch nichts geladen."
      : treffer.length > zeigen.length ? `${zeigen.length} von ${treffer.length} angezeigt – Suche eingrenzen.`
      : `${treffer.length} Startplätze` + (pgeOrt ? ", nach Entfernung sortiert." : ".") + " Windrose: kräftig = gute Startrichtung, blass = möglich.";
  }

  function pgeInfo(text) {
    const el = document.getElementById("pgeInfo");
    if (!el) return;
    if (text) { el.textContent = text; return; }
    const n = N.spAnzahl(), t = N.spStand();
    el.textContent = n ? n + " Startplätze gespeichert (Stand " + new Date(t).toLocaleDateString("de-DE") + ")." : "Noch keine Startplätze aus dem Internet geladen.";
  }
  function pgeLaden() {
    const b = document.getElementById("pgeLaden");
    b.disabled = true;
    pgeInfo("Suche Standort …");
    navigator.geolocation.getCurrentPosition(async p => {
      pgeInfo("Lade Startplätze …");
      try { const n = await rufen("spLaden", p.coords.latitude, p.coords.longitude, 150); melden(n + " Startplätze gespeichert"); pgeInfo();
            if (!document.getElementById("pgeListeBox").hidden) pgeListe(); }
      catch (e) { pgeInfo(e.message); }
      b.disabled = false;
      if (typeof naechsterStartplatz === "function") naechsterStartplatz(true);
    }, () => { pgeInfo("Kein Standort – GPS einschalten und nochmal versuchen."); b.disabled = false; },
    { enableHighAccuracy: false, timeout: 20000, maximumAge: 600000 });
  }

  // ---------------- Wo ist mein Vario? ----------------
  function zuletzt() { try { const t = N.zuletztGesehen && N.zuletztGesehen(); return t ? JSON.parse(t) : null; } catch (e) { return null; } }
  let ich = null, ichHolen = 0;
  function abstand(a, b) {
    const r = Math.PI / 180, dLat = (b.lat - a.lat) * r, dLon = (b.lon - a.lon) * r;
    const h = Math.sin(dLat / 2) ** 2 + Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dLon / 2) ** 2;
    return 12742000 * Math.asin(Math.sqrt(h));
  }
  function richtung(a, b) {
    const r = Math.PI / 180, y = Math.sin((b.lon - a.lon) * r) * Math.cos(b.lat * r);
    const x = Math.cos(a.lat * r) * Math.sin(b.lat * r) - Math.sin(a.lat * r) * Math.cos(b.lat * r) * Math.cos((b.lon - a.lon) * r);
    const g = (Math.atan2(y, x) / r + 360) % 360;
    return ["Norden", "Nordosten", "Osten", "Südosten", "Süden", "Südwesten", "Westen", "Nordwesten"][Math.round(g / 45) % 8];
  }
  function woZeigen() {
    const box = document.getElementById("woIstVario");
    if (!box) return;
    const z = zuletzt();
    const seiteSichtbar = !document.getElementById("seite_geraet").hidden;
    box.hidden = !z || N.istVerbunden();
    if (box.hidden || !seiteSichtbar) return;
    // eigene Position (hoechstens alle 5 s fragen)
    if (navigator.geolocation && Date.now() - ichHolen > 5000) {
      ichHolen = Date.now();
      navigator.geolocation.getCurrentPosition(p => { ich = { lat: p.coords.latitude, lon: p.coords.longitude }; woZeigen(); },
        () => {}, { enableHighAccuracy: true, timeout: 15000, maximumAge: 5000 });
    }
    const d = new Date(z.zeit), heute = new Date().toDateString() === d.toDateString();
    const wann = (heute ? "heute " : d.toLocaleDateString("de-DE", { weekday: "short", day: "2-digit", month: "2-digit" }) + " ") +
      d.toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" });
    let text = z.name + " war zuletzt " + wann + " verbunden.";
    if (ich) {
      const m = abstand(ich, z);
      text += m < 20 ? " Du bist ganz in der Nähe." :
        " Etwa " + (m < 1000 ? Math.round(m / 10) * 10 + " m" : (m / 1000).toFixed(1).replace(".", ",") + " km") + " Richtung " + richtung(ich, z) + ".";
    }
    text += " Es liegt meist im orangen Kreis (± 50 m).";
    document.getElementById("woText").textContent = text;
    woKarte(z, ich);
  }
  const kachelSpeicher = {};
  function woKarte(z, ich) {
    const c = document.getElementById("woKarte"), dpr = window.devicePixelRatio || 1;
    const B = c.clientWidth, H = c.clientHeight;
    if (!B) return;
    c.width = B * dpr; c.height = H * dpr;
    const g = c.getContext("2d"); g.scale(dpr, dpr);
    const welt = (lat, lon, s) => { const r = lat * Math.PI / 180; return [(lon + 180) / 360 * s, (1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2 * s]; };
    // Zoom: beide Punkte sichtbar, hoechstens Stufe 17
    let zoom = 17;
    if (ich) {
      while (zoom > 3) {
        const s = 256 * Math.pow(2, zoom), a = welt(z.lat, z.lon, s), b = welt(ich.lat, ich.lon, s);
        if (Math.abs(a[0] - b[0]) < B - 60 && Math.abs(a[1] - b[1]) < H - 60) break;
        zoom--;
      }
    }
    const s = 256 * Math.pow(2, zoom);
    const a = welt(z.lat, z.lon, s), b = ich ? welt(ich.lat, ich.lon, s) : a;
    const ox = (a[0] + b[0]) / 2 - B / 2, oy = (a[1] + b[1]) / 2 - H / 2;
    const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
    const malen = () => {
      g.fillStyle = css("--chip"); g.fillRect(0, 0, B, H);
      for (let tx = Math.floor(ox / 256); tx <= Math.floor((ox + B) / 256); tx++)
        for (let ty = Math.floor(oy / 256); ty <= Math.floor((oy + H) / 256); ty++) {
          const k = zoom + "/" + tx + "/" + ty;
          let img = kachelSpeicher[k];
          if (!img) { img = kachelSpeicher[k] = new Image(); img.onload = malen; img.src = "https://tile.openstreetmap.org/" + k + ".png"; }
          if (img.complete && img.naturalWidth) g.drawImage(img, tx * 256 - ox, ty * 256 - oy);
        }
      const punkt = (p, fa, r) => { g.beginPath(); g.arc(p[0] - ox, p[1] - oy, r, 0, 7); g.fillStyle = fa; g.fill(); g.lineWidth = 2.5; g.strokeStyle = "#000"; g.stroke(); };
      // Ungenauigkeit als Kreis
      const mProPx = 40075016 * Math.cos(z.lat * Math.PI / 180) / s;
      g.beginPath(); g.arc(a[0] - ox, a[1] - oy, Math.max(12, 50 / mProPx), 0, 7);
      g.fillStyle = "rgba(255,162,76,.22)"; g.fill(); g.strokeStyle = css("--accent"); g.lineWidth = 1.5; g.stroke();
      if (ich) punkt(b, "#ffffff", 7);
      punkt(a, css("--accent"), 9);
    };
    malen();
  }

  function statusZeigen() {
    const info = document.getElementById("appTempoInfo");
    if (!info) return;
    if (!N.gpsErlaubt()) info.textContent = "Standort ist nicht erlaubt – bitte in den App-Einstellungen freigeben.";
    else if (!N.istVerbunden()) info.textContent = "Wird gesendet, sobald das Vario verbunden ist – auch bei ausgeschaltetem Bildschirm.";
    else {
      const t = N.tempo();
      info.textContent = t < 0 ? "Warte auf GPS-Signal …" : "Aktuell " + Math.round(t) + " km/h";
    }
    const akku = document.getElementById("appAkkuZeile");
    if (akku) akku.hidden = N.akkuFrei();
    // GPS-Tempo auch in der Live-Karte
    const box = document.getElementById("lgsBox"), gs = document.getElementById("lgs");
    if (box && gs) {
      const t = N.tempo(), an = N.tempoAn();
      box.hidden = !an;
      let txt = t < 0 ? "–" : Math.round(t) + " km/h";
      try {
        const w = N.wind && N.wind() ? JSON.parse(N.wind()) : null;
        if (w) {
          const R = ["N", "NO", "O", "SO", "S", "SW", "W", "NW"];
          txt += w.kmh < 3 ? " · Wind still" : ` · Wind ${R[Math.round(w.aus / 45) % 8]} ${w.kmh}`;
        }
      } catch (e) {}
      gs.textContent = txt;
    }
  }

  // Versionen vergleichen: "2.10.18" < "2.10.19"
  function neuer(a, b) {
    const x = String(a || "0").split(".").map(Number), y = String(b || "0").split(".").map(Number);
    for (let i = 0; i < Math.max(x.length, y.length); i++) {
      if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0);
    }
    return false;
  }

  let verfuegbar = null;
  async function fwSuchen() {
    versionenPruefen();
    const info = document.getElementById("fwInfo");
    const start = document.getElementById("fwStart");
    start.hidden = true;
    start.textContent = "Jetzt aktualisieren";
    info.textContent = "Suche …";
    try { verfuegbar = await rufen("neuesteFirmware"); }
    catch (e) { info.textContent = "Keine Verbindung zum Internet."; return; }
    const v = (typeof varioInfoDaten === "object" && varioInfoDaten) ? varioInfoDaten : {};
    if (!N.istVerbunden() || !v.fw) {
      info.textContent = "Neueste Firmware: " + verfuegbar + ". Zum Aktualisieren mit dem Vario verbinden. Hängt ein Vario im Update-Modus, kannst du es hier direkt neu bespielen.";
      start.hidden = false;
      start.textContent = "Vario im Update-Modus bespielen";
      return;
    }
    dvMerken();
    if (!v.dv) {
      info.textContent = "Auf dem Vario ist " + v.fw + ". Diese Version braucht einmal ein Update per USB-Kabel – danach geht es per Bluetooth.";
      return;
    }
    if (neuer(verfuegbar, v.fw)) {
      info.textContent = "Auf dem Vario: " + v.fw + " · neu: " + verfuegbar;
      start.hidden = false;
    } else {
      info.textContent = "Das Vario ist aktuell (" + v.fw + ").";
      start.hidden = false;
      start.textContent = "Trotzdem neu aufspielen";
    }
  }

  function dvMerken() {
    const v = (typeof varioInfoDaten === "object" && varioInfoDaten) ? varioInfoDaten : {};
    if (v.dv) try { localStorage.setItem("sweefDv", v.dv); } catch (e) {}
    try { return v.dv || localStorage.getItem("sweefDv") || "g"; } catch (e) { return v.dv || "g"; }
  }

  function fwStarten() {
    const dv = dvMerken();
    if (N.istVerbunden()) {
      if (typeof entsperrt !== "undefined" && !entsperrt) { melden("Erst entsperren"); return; }
      if (!confirm("Firmware jetzt aufspielen? Das Vario startet dabei neu.")) return;
    } else {
      if (!confirm("Kein Vario verbunden. Hängt ein Vario noch im Update-Modus („Update per Handy“ auf dem Display)? Dann jetzt das Update dort fortsetzen?")) return;
    }
    document.getElementById("fwStart").hidden = true;
    document.getElementById("fwSuchen").disabled = true;
    N.updateStarten(dv === "k" ? "k" : "g");
  }

  function updateZeigen(d) {
    const info = document.getElementById("fwInfo");
    const balken = document.getElementById("fwBalken");
    if (!info) return;
    info.textContent = d.text;
    if (d.art === "fortschritt" || d.art === "fertig") {
      balken.hidden = false;
      balken.querySelector("i").style.width = d.p + "%";
    }
    if (d.art === "fertig" || d.art === "fehler") {
      document.getElementById("fwSuchen").disabled = false;
      if (d.art === "fertig") setTimeout(() => { balken.hidden = true; }, 3000);
      melden(d.text);
    }
  }

  // ---------------- Neue Versionen (App und Firmware) ----------------
  let neueste = null;          // {app, appUrl, firmware, eigene}
  let letztePruefung = 0;
  let fwHinweisWeg = false;

  function hinweisLeiste(text, knopf, aktion, wegKlick) {
    let l = document.getElementById("hinweisLeiste");
    if (!l) {
      l = document.createElement("div");
      l.id = "hinweisLeiste";
      l.className = "hinweis";
      const ziel = document.getElementById("display");
      if (ziel) ziel.parentNode.insertBefore(l, ziel.nextSibling); else document.body.prepend(l);
    }
    l.innerHTML = "";
    const t = document.createElement("span");
    t.id = "hinweisText"; t.style.flex = "1"; t.textContent = text;
    const b = document.createElement("button");
    b.textContent = knopf; b.onclick = aktion;
    const x = document.createElement("button");
    x.textContent = "Später"; x.onclick = () => { l.remove(); if (wegKlick) wegKlick(); };
    l.append(t, b, x);
  }

  async function versionenPruefen() {
    letztePruefung = Date.now();
    try { neueste = JSON.parse(await rufen("versionenPruefen")); } catch (e) { return; }
    const appNeu = document.getElementById("appNeu"), appV = document.getElementById("appVersion");
    const istNeu = neueste.app > 0 && neueste.app > neueste.eigene && neueste.appUrl;
    if (appNeu) appNeu.hidden = !istNeu;
    if (appV) appV.textContent = N.appVersion() + (istNeu ? " → 1.0." + neueste.app + " ist da" : " · aktuell");
    if (istNeu) {
      hinweisLeiste("Neue Sweef-App 1.0." + neueste.app + " ist da.", "Installieren",
        () => N.appInstallieren(neueste.appUrl));
    }
  }

  // Firmware: sobald ein Vario verbunden ist, Version vergleichen
  setInterval(() => {
    if (!neueste || fwHinweisWeg || !N.istVerbunden()) return;
    if (document.getElementById("hinweisLeiste")) return;     // App-Hinweis hat Vorrang
    const v = (typeof varioInfoDaten === "object" && varioInfoDaten) ? varioInfoDaten : {};
    if (!v.fw || !v.dv || !neueste.firmware) return;
    if (neuer(neueste.firmware, v.fw)) {
      fwHinweisWeg = true;
      hinweisLeiste("Neue Vario-Firmware " + neueste.firmware + " (auf dem Vario: " + v.fw + ").", "Ansehen",
        () => { const l = document.getElementById("hinweisLeiste"); if (l) l.remove();
                if (typeof reiter === "function") reiter("geraet"); fwSuchen(); });
    }
  }, 3000);

  // Hinweis unten auf der Seite anpassen
  function hinweisAnpassen() {
    document.querySelectorAll("p.note").forEach(p => {
      if (p.textContent.indexOf("Funktioniert in Chrome") === 0) p.textContent = "Sweef-App " + N.appVersion() + " für Android";
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", () => { bereichBauen(); hinweisAnpassen(); });
  } else { bereichBauen(); hinweisAnpassen(); }
})();

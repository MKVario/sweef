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
      box.style.cssText = "background:#13232a;color:#e8f1f2;border-radius:12px;padding:16px;width:100%;max-width:360px;font:inherit";
      box.innerHTML = "<h2 style='margin:0 0 10px'>Vario auswählen</h2>";
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
      <h2>App</h2>
      <div class="feld">
        <label style="display:flex;gap:10px;align-items:center">
          <input type="checkbox" id="appTempo"> Geschwindigkeit (GPS) ans Vario senden
        </label>
        <div class="note" id="appTempoInfo"></div>
      </div>
      <div class="feld">
        <div class="note">Damit die Verbindung im Hintergrund nicht abreißt, sollte die Akku-Optimierung für die Sweef-App aus sein.</div>
        <div><button id="appAkku">Akku-Optimierung ausschalten</button></div>
      </div>
      <h2>Firmware-Update</h2>
      <p class="hint" id="fwInfo">Mit dem Vario verbinden, dann nach Updates suchen.</p>
      <div class="fortschritt" id="fwBalken" hidden><i></i></div>
      <div class="knopfreihe">
        <button id="fwSuchen">Nach Update suchen</button>
        <button class="primary" id="fwStart" hidden>Jetzt aktualisieren</button>
      </div>
      <p class="note">Das Update dauert 1–2 Minuten. Vario dabei eingeschaltet und nah am Handy lassen. Bricht es ab, einfach nochmal starten.</p>
      <p class="note" id="appVersion"></p>`;
    seite.insertBefore(s, seite.firstChild);

    const tempo = document.getElementById("appTempo");
    tempo.checked = N.tempoAn();
    tempo.onchange = () => N.tempoSetzen(tempo.checked);
    document.getElementById("appAkku").onclick = () => N.akkuOptimierungAus();
    document.getElementById("appVersion").textContent = "Sweef-App " + N.appVersion();
    document.getElementById("fwSuchen").onclick = fwSuchen;
    document.getElementById("fwStart").onclick = fwStarten;
    setInterval(statusZeigen, 1000);
    statusZeigen();
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
    const akku = document.getElementById("appAkku");
    if (akku) akku.hidden = N.akkuFrei();
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

  // Hinweis unten auf der Seite anpassen
  function hinweisAnpassen() {
    document.querySelectorAll("p.note").forEach(p => {
      if (p.textContent.indexOf("Funktioniert in Chrome") === 0) p.textContent = "Sweef-App für Android.";
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", () => { bereichBauen(); hinweisAnpassen(); });
  } else { bereichBauen(); hinweisAnpassen(); }
})();

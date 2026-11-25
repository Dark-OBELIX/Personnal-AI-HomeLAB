/*
 * led-strip-card — carte de tableau de bord Home Assistant pour un bandeau LED.
 *
 * Interrupteur, aperçu, 12 couleurs rapides, couleur personnalisée, luminosité
 * et presets. Fonctionne avec n'importe quelle entité light en couleur RGB ;
 * les presets sont affichés si les entités select/button du firmware led_web existent.
 *
 * Installation : copier ce fichier dans /config/www/, puis Paramètres → Tableaux de bord
 * → ⋮ → Ressources → Ajouter : URL /local/led-strip-card.js, type « Module JavaScript ».
 *
 * Configuration :
 *   type: custom:led-strip-card
 *   entity: light.bandeau_chambre
 *   name: Bandeau chambre               # optionnel
 *   preset_entity: select.xxx            # optionnel (déduit de l'entité light sinon)
 *   save_entity: button.xxx              # optionnel (déduit de l'entité light sinon)
 *   transition: 0.5                      # optionnel, en secondes
 *   colors: ["#ffffff", "#ff0000", ...]  # optionnel, remplace les 12 couleurs rapides
 */

const CARD_VERSION = "1.0.0";

const DEFAULT_COLORS = [
  "#ffffff", "#ffb46b", "#ff0000", "#ff7a00", "#ffe600", "#00ff40",
  "#00e5ff", "#0040ff", "#8000ff", "#ff00c8", "#ff4060", "#40ffb0",
];

const NO_PRESET = "Aucun";
const SEND_INTERVAL_MS = 250; // une commande au plus tous les 250 ms pendant un glissement

const hexToRgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
const rgbToHex = (rgb) => "#" + rgb.map((v) => Math.round(v).toString(16).padStart(2, "0")).join("");

class LedStripCard extends HTMLElement {
  setConfig(config) {
    if (!config || typeof config.entity !== "string" || !config.entity.startsWith("light.")) {
      throw new Error("Indique une entité lumière : entity: light.xxx");
    }
    const objectId = config.entity.slice("light.".length);
    this._config = {
      colors: DEFAULT_COLORS,
      preset_entity: `select.${objectId}_preset`,
      save_entity: `button.${objectId}_enregistrer_comme_preset`,
      ...config,
    };
    this._color = "#ffffff";
    this._brightness = 255;
    this._editing = false; // vrai pendant qu'on manipule un curseur : on n'écrase pas la valeur
    this._lastSend = 0;
    this._pendingTimer = null;
    this._render();
  }

  set hass(hass) {
    this._hass = hass;
    this._update();
  }

  getCardSize() {
    return 7;
  }

  static getStubConfig(hass) {
    const light = Object.keys(hass.states).find((id) => id.startsWith("light."));
    return { entity: light || "light.bandeau_chambre" };
  }

  /* ---------- Construction ---------- */

  _render() {
    if (!this.shadowRoot) {
      this.attachShadow({ mode: "open" });
    }
    this.shadowRoot.innerHTML = `
      <style>
        ha-card { padding: 16px; }
        header { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 14px; }
        .name { font-size: 1.15em; font-weight: 500; color: var(--primary-text-color); }
        .status { font-size: .85em; color: var(--secondary-text-color); margin-top: 2px; }
        .preview { height: 56px; border-radius: 12px; margin-bottom: 16px; background: #000;
                   transition: background .2s, box-shadow .2s, opacity .2s; }
        .section { margin-bottom: 16px; }
        .label { display: flex; justify-content: space-between; font-size: .8em; letter-spacing: .05em;
                 text-transform: uppercase; color: var(--secondary-text-color); margin-bottom: 8px; }
        .swatches { display: grid; grid-template-columns: repeat(6, 1fr); gap: 8px; }
        .swatch { aspect-ratio: 1; border-radius: 50%; border: 3px solid transparent; cursor: pointer; padding: 0;
                  box-shadow: inset 0 0 0 1px rgba(0,0,0,.15); }
        .swatch.active { border-color: var(--primary-text-color); }
        .swatch:focus-visible, .chip:focus-visible, button.save:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; }
        .custom { display: flex; align-items: center; gap: 10px; margin-top: 10px; font-size: .9em;
                  color: var(--secondary-text-color); }
        input[type=color] { width: 44px; height: 32px; border: none; border-radius: 8px; background: none; cursor: pointer; padding: 0; }
        input[type=range] { width: 100%; accent-color: var(--primary-color); }
        .chips { display: flex; flex-wrap: wrap; gap: 8px; }
        .chip { display: inline-flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 999px;
                border: 1px solid var(--divider-color); background: none; color: var(--primary-text-color);
                cursor: pointer; font: inherit; font-size: .9em; }
        .chip.active { border-color: var(--primary-color); color: var(--primary-color); }
        .chip .dot { width: 12px; height: 12px; border-radius: 50%; background: var(--secondary-text-color); }
        button.save { margin-top: 10px; width: 100%; padding: 10px; border-radius: 10px; cursor: pointer; font: inherit;
                      border: 1px dashed var(--divider-color); background: none; color: var(--primary-text-color); }
        .off .dim { opacity: .45; }
        .warning { color: var(--error-color); font-size: .9em; }
        [hidden] { display: none !important; }
      </style>
      <ha-card>
        <div id="root">
          <header>
            <div>
              <div class="name" id="name"></div>
              <div class="status" id="status"></div>
            </div>
            <ha-switch id="power"></ha-switch>
          </header>
          <div class="warning" id="warning" hidden></div>
          <div class="preview" id="preview"></div>
          <div class="section dim">
            <div class="label"><span>Couleur</span></div>
            <div class="swatches" id="swatches"></div>
            <label class="custom">
              <input type="color" id="color" value="#ffffff">
              <span>Couleur personnalisée</span>
            </label>
          </div>
          <div class="section dim">
            <div class="label"><span>Luminosité</span><span id="brVal">100 %</span></div>
            <input type="range" id="brightness" min="1" max="255" value="255" aria-label="Luminosité">
          </div>
          <div class="section" id="presetSection" hidden>
            <div class="label"><span>Presets</span></div>
            <div class="chips" id="presets"></div>
            <button class="save" id="save" hidden>+ Enregistrer comme preset</button>
          </div>
        </div>
      </ha-card>`;

    const $ = (id) => this.shadowRoot.getElementById(id);
    this._$ = $;

    const swatches = $("swatches");
    this._config.colors.forEach((c) => {
      const b = document.createElement("button");
      b.className = "swatch";
      b.style.background = c;
      b.dataset.color = c.toLowerCase();
      b.title = c;
      b.setAttribute("aria-label", `Couleur ${c}`);
      b.addEventListener("click", () => {
        this._color = c.toLowerCase();
        this._send({ rgb_color: hexToRgb(this._color) }, true);
        this._paint();
      });
      swatches.appendChild(b);
    });

    $("power").addEventListener("change", (e) => {
      if (e.target.checked) {
        this._call("light", "turn_on", this._withTransition({}));
      } else {
        this._call("light", "turn_off", this._withTransition({}));
      }
    });

    const color = $("color");
    color.addEventListener("input", (e) => {
      this._editing = true;
      this._color = e.target.value;
      this._paint();
      this._send({ rgb_color: hexToRgb(this._color) });
    });
    color.addEventListener("change", (e) => {
      this._editing = false;
      this._send({ rgb_color: hexToRgb(e.target.value) }, true);
    });

    const br = $("brightness");
    br.addEventListener("input", (e) => {
      this._editing = true;
      this._brightness = +e.target.value;
      this._paint();
      this._send({ brightness: this._brightness });
    });
    br.addEventListener("change", (e) => {
      this._editing = false;
      this._send({ brightness: +e.target.value }, true);
    });

    $("save").addEventListener("click", () => {
      this._call("button", "press", { entity_id: this._config.save_entity });
    });

    this._update();
  }

  /* ---------- Mise à jour depuis Home Assistant ---------- */

  _update() {
    if (!this._hass || !this._$) return;
    const $ = this._$;
    const st = this._hass.states[this._config.entity];

    $("warning").hidden = !!st;
    if (!st) {
      $("warning").textContent = `Entité introuvable : ${this._config.entity}`;
      return;
    }

    this._on = st.state === "on";
    this._unavailable = st.state === "unavailable";
    if (!this._editing) {
      if (Array.isArray(st.attributes.rgb_color)) this._color = rgbToHex(st.attributes.rgb_color);
      if (typeof st.attributes.brightness === "number") this._brightness = st.attributes.brightness;
    }

    $("name").textContent = this._config.name || st.attributes.friendly_name || this._config.entity;
    $("status").textContent = this._unavailable ? "Indisponible" : this._on ? "Allumé" : "Éteint";
    $("power").checked = this._on;
    $("power").disabled = this._unavailable;

    this._updatePresets();
    this._paint();
  }

  _updatePresets() {
    const $ = this._$;
    const sel = this._hass.states[this._config.preset_entity];
    const save = this._hass.states[this._config.save_entity];
    $("presetSection").hidden = !sel && !save;
    $("save").hidden = !save;

    const box = $("presets");
    const options = sel ? (sel.attributes.options || []).filter((o) => o !== NO_PRESET) : [];
    const key = JSON.stringify([options, sel && sel.state, this._on]);
    if (key === this._presetKey) return; // évite de reconstruire à chaque mise à jour de HA
    this._presetKey = key;

    box.replaceChildren();
    options.forEach((opt) => {
      const chip = document.createElement("button");
      chip.className = "chip" + (this._on && sel.state === opt ? " active" : "");
      const dot = document.createElement("span");
      dot.className = "dot";
      const label = document.createElement("span");
      label.textContent = opt; // textContent : pas d'injection HTML depuis MQTT
      chip.append(dot, label);
      chip.addEventListener("click", () =>
        this._call("select", "select_option", { entity_id: this._config.preset_entity, option: opt }));
      box.appendChild(chip);
    });
  }

  _paint() {
    const $ = this._$;
    const pct = Math.round((this._brightness / 255) * 100);
    $("root").classList.toggle("off", !this._on);
    $("brVal").textContent = `${pct} %`;
    if (!this._editing) {
      $("color").value = this._color;
      $("brightness").value = this._brightness;
    }
    const p = $("preview");
    p.style.background = this._on ? this._color : "#000";
    p.style.opacity = this._on ? 0.3 + 0.7 * (this._brightness / 255) : 1;
    p.style.boxShadow = this._on ? `0 0 ${8 + pct / 3}px ${this._color}` : "none";
    this.shadowRoot.querySelectorAll(".swatch").forEach((s) =>
      s.classList.toggle("active", s.dataset.color === this._color.toLowerCase()));
  }

  /* ---------- Envoi des commandes ---------- */

  _withTransition(data) {
    const out = { entity_id: this._config.entity, ...data };
    if (typeof this._config.transition === "number") out.transition = this._config.transition;
    return out;
  }

  // Limite le débit pendant un glissement ; la dernière valeur est toujours envoyée.
  _send(data, immediate = false) {
    this._pendingData = { ...(this._pendingData || {}), ...data };
    const now = Date.now();
    const flush = () => {
      this._pendingTimer = null;
      this._lastSend = Date.now();
      const payload = this._pendingData;
      this._pendingData = null;
      if (payload) this._call("light", "turn_on", this._withTransition(payload));
    };
    if (immediate || now - this._lastSend >= SEND_INTERVAL_MS) {
      clearTimeout(this._pendingTimer);
      flush();
    } else if (!this._pendingTimer) {
      this._pendingTimer = setTimeout(flush, SEND_INTERVAL_MS - (now - this._lastSend));
    }
  }

  _call(domain, service, data) {
    if (!this._hass) return;
    this._hass.callService(domain, service, data).catch((err) => {
      const w = this._$("warning");
      w.hidden = false;
      w.textContent = `Erreur : ${err.message || err}`;
    });
  }
}

if (!customElements.get("led-strip-card")) {
  customElements.define("led-strip-card", LedStripCard);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "led-strip-card",
    name: "Bandeau LED",
    description: "Couleur, luminosité et presets d'un bandeau LED",
    preview: false,
  });
  console.info(`%c led-strip-card %c ${CARD_VERSION} `, "background:#333;color:#fff", "background:#ffb46b;color:#000");
}

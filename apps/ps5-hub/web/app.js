"use strict";

const get = (id) => document.getElementById(id);
const navigation = get("navigation");
const dialog = get("game-dialog");
const search = get("search");
const providerFilter = get("provider-filter");
const availabilityFilter = get("availability-filter");
const modeFilter = get("mode-filter");
const state = {
  view: "discover",
  catalog: { games: [], sources: [], errors: [] },
  library: { games: [] },
  status: { jobs: [], runtimes: [] },
  compatibility: { records: [] },
  loaded: new Set(),
  errors: new Map(),
  syncing: false,
  detailId: null,
  action: null,
  actionError: null,
  confirmingUninstall: false,
};

const stateLabels = {
  queued: "Wartet", pending: "Wartet", running: "Läuft", downloading: "Wird heruntergeladen",
  verifying: "Wird geprüft", installing: "Wird installiert", failed: "Fehlgeschlagen",
  error: "Fehlgeschlagen", complete: "Abgeschlossen", completed: "Abgeschlossen",
  installed: "Installiert", success: "Erfolgreich", done: "Abgeschlossen", ready: "Bereit",
  available: "Verfügbar", synced: "Aktualisiert", ok: "Erreichbar", disabled: "Deaktiviert",
  unsupported: "Nicht unterstützt", local: "Lokal", idle: "Bereit", syncing: "Wird aktualisiert",
};
const activeStates = new Set(["queued", "pending", "running", "downloading", "verifying", "installing"]);
const errorStates = new Set(["failed", "error"]);
const quantity = new Intl.NumberFormat("de-DE");
const formatBytes = new Intl.NumberFormat("de-DE", { style: "unit", unit: "megabyte", maximumFractionDigits: 2 });
const formatDate = new Intl.DateTimeFormat("de-DE", { dateStyle: "medium" });

function make(tag, className, label) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (label !== undefined) node.textContent = label;
  return node;
}

function elementList(container, nodes, fallback) {
  container.replaceChildren(...(nodes.length ? nodes : [make("p", "empty-state", fallback)]));
}

function labelFor(key) {
  return stateLabels[key] ?? key ?? "Unbekannt";
}

function isInstallable(game) {
  return game.installable ?? Boolean(game.runtime && game.entrypoint && game.archive_url && game.sha256);
}

function providerName(game) {
  return game.provider ?? game.source ?? "Quelle unbekannt";
}

function installedGame(id) {
  return state.library.games.find((game) => game.id === id);
}

function catalogGame(id) {
  return state.catalog.games.find((game) => game.id === id);
}

function gameTitle(id) {
  return (catalogGame(id) ?? installedGame(id))?.title ?? id;
}

function jobFor(id) {
  return state.status.jobs.find((job) => job.id === id);
}

async function request(path, data) {
  const response = await fetch("/api/" + path, data === undefined ? {} : {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
  const body = await response.json().catch(() => null);
  if (!response.ok) {
    throw new Error(body?.error?.message ?? body?.error ?? body?.message ?? ("HTTP " + response.status));
  }
  if (!body) throw new Error("Der Hub hat keine Daten geliefert.");
  return body;
}

let announcementTimer;
function announce(message) {
  const announcement = get("announcement");
  announcement.textContent = message;
  announcement.hidden = false;
  clearTimeout(announcementTimer);
  announcementTimer = setTimeout(() => { announcement.hidden = true; }, 5000);
}

function setView(view, focus = false) {
  const target = navigation.querySelector('[data-view="' + view + '"]');
  if (!target) return;
  state.view = view;
  for (const button of navigation.querySelectorAll("[data-view]")) {
    const active = button === target;
    get(button.dataset.view).hidden = !active;
    if (active) button.setAttribute("aria-current", "page");
    else button.removeAttribute("aria-current");
  }
  get("view-label").textContent = target.childNodes[1].textContent;
  window.scrollTo(0, 0);
  if (focus) target.focus();
}

function artwork(game, url) {
  if (!["all", "teen"].includes(game.age_rating) || typeof url !== "string") return null;
  let location;
  try {
    location = new URL(url);
  } catch {
    return null;
  }
  if (location.origin !== "https://raw.githubusercontent.com" || location.username || location.password) return null;
  const image = make("img", "catalog-image");
  image.alt = "";
  image.loading = "lazy";
  image.referrerPolicy = "no-referrer";
  image.addEventListener("error", () => image.remove(), { once: true });
  image.src = location.href;
  return image;
}

function card(game) {
  const button = make("button", "game-card");
  button.type = "button";
  const art = make("span", "game-art" + (game.featured ? " featured" : ""));
  art.setAttribute("aria-hidden", "true");
  art.append(artwork(game, game.cover_url) ?? make("span"));
  const info = make("span", "card-info");
  const meta = [providerName(game), game.runtime?.toUpperCase()].filter(Boolean).join(" · ");
  const installed = installedGame(game.id);
  const status = installed ? "Installiert" : isInstallable(game) ? "Installierbar" : "Katalogeintrag";
  info.append(make("span", "card-title", game.title), make("span", "card-meta", meta), make("span", "card-status", status));
  button.append(art, info);
  button.dataset.game = game.id;
  button.addEventListener("click", () => showDetail(game.id));
  return button;
}

function matchingGames() {
  const term = search.value.trim().toLocaleLowerCase("de-DE");
  return state.catalog.games.filter((game) => {
    const searchable = [game.title, game.description, providerName(game), game.runtime].join(" ").toLocaleLowerCase("de-DE");
    return (!term || searchable.includes(term)) && (!providerFilter.value || providerName(game) === providerFilter.value)
      && (availabilityFilter.value !== "installable" || isInstallable(game))
      && (availabilityFilter.value !== "installed" || installedGame(game.id))
      && (!modeFilter.value || game.modes?.includes(modeFilter.value));
  });
}

function updateCards(id, games, fallback) {
  const container = get(id);
  const focusedId = container.contains(document.activeElement) ? document.activeElement.dataset.game : null;
  const cards = games.map(card);
  elementList(container, cards, fallback);
  if (focusedId) {
    const replacement = cards.find((item) => item.dataset.game === focusedId);
    replacement?.focus({ preventScroll: true });
  }
}

function renderCatalog() {
  const previousProvider = providerFilter.value;
  const providers = [...new Set(state.catalog.games.map(providerName))].sort((first, second) => first.localeCompare(second, "de-DE"));
  providerFilter.replaceChildren(new Option("Alle Quellen", ""), ...providers.map((provider) => new Option(provider, provider)));
  if (providers.includes(previousProvider)) providerFilter.value = previousProvider;
  const featured = state.catalog.games.find((game) => game.featured);
  get("hero-game").hidden = !featured;
  get("hero-game").dataset.game = featured?.id ?? "";
  get("discover-title").textContent = featured?.title ?? "Spiele entdecken";
  get("hero-description").textContent = featured?.description ?? "Durchsuche den Katalog und lege deine lokale Bibliothek an.";
  const sync = get("sync-button");
  sync.disabled = state.syncing;
  sync.lastChild.textContent = state.syncing ? " Katalog wird aktualisiert …" : " Katalog aktualisieren";
  renderCatalogMatches();
}

function renderCatalogMatches() {
  const games = matchingGames();
  get("catalog-count").textContent = state.loaded.has("catalog") ? quantity.format(games.length) + (games.length === 1 ? " Spiel" : " Spiele") : "";
  const trouble = get("catalog-errors");
  const errors = state.catalog.errors ?? [];
  if (state.errors.has("catalog")) {
    trouble.replaceChildren(make("p", "notice", "Katalog zurzeit nicht erreichbar. " + state.errors.get("catalog")));
  } else if (errors.length) {
    const sources = [...new Set(errors.map(({ source }) => source).filter(Boolean))].join(", ");
    trouble.replaceChildren(make("p", "notice", sources ? "Quellen mit Fehlern: " + sources + ". Details unter Diagnose." : "Quellenfehler unter Diagnose."));
  } else {
    trouble.replaceChildren();
  }
  const fallback = state.loaded.has("catalog")
    ? "Für diese Auswahl sind keine Spiele verfügbar."
    : state.errors.has("catalog") ? "Spiele konnten nicht geladen werden." : "Katalog wird geladen …";
  updateCards("catalog-grid", games, fallback);
}

function setCount(id, count) {
  const element = get(id);
  element.hidden = !count;
  element.textContent = quantity.format(count);
}

function renderLibrary() {
  setCount("library-count", state.library.games.length);
  const fallback = state.errors.has("library") ? "Bibliothek konnte nicht geladen werden." : "Hier erscheinen deine installierten Spiele.";
  updateCards("library-grid", state.library.games, fallback);
}

function row(title, status, description, failed = false) {
  const element = make("article", "data-row");
  element.append(make("h3", "", title));
  if (status) element.append(make("span", "row-state", status));
  if (description) element.append(make("p", failed ? "failure" : "", description));
  return element;
}

function byteCount(bytes) {
  return formatBytes.format(bytes / 1000000);
}

function progressFor(job) {
  const progress = make("progress");
  progress.setAttribute("aria-label", "Downloadfortschritt für " + gameTitle(job.id));
  if (Number.isFinite(job.total_bytes) && job.total_bytes > 0 && Number.isFinite(job.downloaded_bytes)) {
    progress.max = job.total_bytes;
    progress.value = job.downloaded_bytes;
  }
  return progress;
}

function renderStatus() {
  const { platform, runtimes, jobs } = state.status;
  get("platform").textContent = platform ?? "Host";
  setCount("download-count", jobs.filter((job) => activeStates.has(job.state)).length);
  const downloads = jobs.map((job) => {
    const bytes = Number.isFinite(job.downloaded_bytes) ? byteCount(job.downloaded_bytes) : "";
    const total = Number.isFinite(job.total_bytes) && job.total_bytes > 0 ? byteCount(job.total_bytes) : "";
    const message = job.error || [bytes, total].filter(Boolean).join(" / ");
    const action = job.action === "uninstall" ? "Deinstallation" : "Installation";
    const element = row(gameTitle(job.id), labelFor(job.state), message ? action + " · " + message : action, errorStates.has(job.state));
    if (job.action !== "uninstall" && activeStates.has(job.state)) element.append(progressFor(job));
    return element;
  });
  elementList(get("job-list"), downloads, state.errors.has("status") ? "Downloadstatus konnte nicht geladen werden." : "Noch keine Downloads in dieser Sitzung.");

  const runtimeRows = runtimes.map((runtime) => row(
    [runtime.name ?? runtime.id, runtime.version].filter(Boolean).join(" · "),
    labelFor(runtime.state), runtime.description,
  ));
  elementList(get("runtime-list"), runtimeRows, state.errors.has("status") ? "Runtime-Status konnte nicht geladen werden." : "Keine Runtime gemeldet.");
}

function renderDiagnostics() {
  const rows = [];
  if (state.status.platform) rows.push(row("Ausführungsplattform", state.status.platform));
  const sources = (state.catalog.sources ?? []).map((source) => {
    const count = Number.isFinite(source.count) ? quantity.format(source.count) + " Katalogeinträge" : "";
    return row(source.name, labelFor(source.state), count);
  });
  rows.push(...sources);
  for (const { source, message } of state.catalog.errors ?? []) {
    rows.push(row(source ?? "Katalogquelle", "Fehler", message, true));
  }
  for (const [name, message] of state.errors) {
    const heading = { catalog: "Katalog", library: "Bibliothek", status: "Hub-Status", compatibility: "Kompatibilitätsdatenbank" }[name];
    rows.push(row(heading ?? name, "Nicht erreichbar", message, true));
  }
  elementList(get("diagnostic-list"), rows, "Noch keine Diagnosedaten geladen.");
  const observations = state.compatibility.records.map((record) => {
    const details = [[record.runtime?.toUpperCase(), record.runtime_version].filter(Boolean).join(" "), record.engine];
    if (record.installed) details.push("Installation erfasst");
    if (record.player_served) details.push("Player ausgeliefert");
    if (record.observed_at) {
      const timestamp = new Date(record.observed_at);
      if (!Number.isNaN(timestamp.getTime())) details.push(formatDate.format(timestamp));
    }
    return row(
      [record.title, record.version].filter(Boolean).join(" · "),
      record.gameplay === "unconfirmed" ? "Gameplay unbestätigt" : labelFor(record.gameplay),
      details.filter(Boolean).join(" · "),
    );
  });
  elementList(get("compatibility-list"), observations, "Noch keine lokalen Beobachtungen.");
}

function finishSync() {
  state.syncing = false;
  const count = state.catalog.errors.length;
  announce(state.errors.get("catalog") ?? (count ? "Katalog teilweise aktualisiert. Details unter Diagnose." : "Katalog aktualisiert."));
  renderCatalog();
  renderDiagnostics();
  renderDetail();
}

function fact(label, value) {
  const element = make("div");
  element.append(make("dt", "", label), make("dd", "", value));
  return element;
}

function detailButton(key, label, style, handler) {
  const button = make("button", style, label);
  button.type = "button";
  button.dataset.action = key;
  button.disabled = state.action !== null;
  button.addEventListener("click", handler);
  return button;
}

function showDetail(id) {
  state.detailId = id;
  state.confirmingUninstall = false;
  state.actionError = null;
  renderDetail();
  if (!dialog.open) dialog.showModal();
}

function renderDetail() {
  const id = state.detailId;
  if (!id) return;
  const installed = installedGame(id);
  const available = catalogGame(id);
  const game = available ?? installed;
  if (!game) {
    if (dialog.open) dialog.close();
    return;
  }
  const currentAction = document.activeElement?.dataset.action;
  const localVersion = installed?.version;
  const job = jobFor(id);
  const isBusy = job && activeStates.has(job.state);
  const failure = state.actionError ?? job?.error;
  const note = get("detail-note");
  get("detail-source").textContent = providerName(game);
  get("detail-title").textContent = game.title;
  get("detail-description").textContent = game.description ?? "";
  get("detail-description").hidden = !game.description;
  const media = [...new Set([game.cover_url, ...(game.screenshots ?? [])])]
    .map((url) => artwork(game, url)).filter(Boolean);
  get("detail-media").replaceChildren(...media);
  const properties = [
    ["Katalogversion", game.version], ["Runtime", game.runtime?.toUpperCase()], ["Lizenz", game.license],
    ["Architektur", game.architecture], ["Entwickler", game.developer], ["Genre", game.genre],
    ["Zielplattform", { pc: "PC", ps5: "PS5" }[game.target_platform]],
    ["Installiert", localVersion], ["Kompatibilität", game.compatibility ?? game.compatibility_status],
    ["Controller", game.controller_support], ["Online", game.multiplayer_support], ["Crossplay", game.crossplay_support],
  ];
  if (Number.isFinite(game.download_size)) properties.push(["Downloadgröße", byteCount(game.download_size)]);
  if (installed?.installed_at) {
    const date = new Date(installed.installed_at);
    if (!Number.isNaN(date.getTime())) properties.push(["Installiert am", formatDate.format(date)]);
  }
  const supportedProperties = properties.filter(([, value]) => typeof value === "string" && value.trim());
  get("detail-facts").replaceChildren(...supportedProperties.map(([label, value]) => fact(label, value)));
  note.classList.toggle("failure", Boolean(failure));
  note.textContent = state.confirmingUninstall ? "Dieses Spiel aus der Bibliothek und vom lokalen Speicher entfernen?"
    : isBusy ? labelFor(job.state)
    : failure ? failure
    : installed ? "Lokal installiert. Spielbarkeit auf PS5 ist unbestätigt."
    : isInstallable(game) ? "Installierbar auf dieser Host-Instanz. Gameplay wurde dadurch noch nicht bestätigt."
    : "Katalogeintrag ohne unterstütztes Installationspaket.";
  const controls = [];
  if (state.confirmingUninstall) {
    controls.push(detailButton("cancel", "Abbrechen", "secondary", () => {
      state.confirmingUninstall = false;
      renderDetail();
      get("detail-actions").querySelector('[data-action="remove"]')?.focus();
    }));
    controls.push(detailButton("confirm", "Deinstallieren", "danger", () => runAction("uninstall", id)));
  } else if (installed && !isBusy) {
    const play = make("a", "primary", "Spielen");
    play.href = "/api/launch?" + new URLSearchParams({ id });
    play.target = "_blank";
    play.rel = "noopener noreferrer";
    play.dataset.action = "play";
    controls.push(play);
    if (available && isInstallable(available)) {
      controls.push(detailButton("install", localVersion === available.version ? "Reparieren" : "Katalogversion installieren", "secondary", () => runAction("install", id)));
    }
    controls.push(detailButton("remove", "Deinstallieren", "danger", () => {
      state.confirmingUninstall = true;
      renderDetail();
      get("detail-actions").querySelector('[data-action="cancel"]')?.focus();
    }));
  } else if (isInstallable(game) && !isBusy) {
    controls.push(detailButton("install", job?.error ? "Erneut installieren" : "Installieren", "primary", () => runAction("install", id)));
  }
  get("detail-actions").replaceChildren(...controls);
  if (currentAction) (controls.find((control) => control.dataset.action === currentAction && !control.disabled) ?? get("detail-close")).focus();
}

async function loadResource(name) {
  try {
    state[name] = await request(name);
    state.loaded.add(name);
    state.errors.delete(name);
  } catch (error) {
    state.errors.set(name, error.message);
  }
}

async function refresh() {
  await Promise.all([
    loadResource("catalog"),
    loadResource("status").then(() => Promise.all(["library", "compatibility"].map(loadResource))),
  ]);
  renderCatalog();
  renderLibrary();
  renderStatus();
  renderDiagnostics();
  renderDetail();
}

let statusPending = false;
let pollingPaused = false;
async function pollStatus() {
  if (statusPending || pollingPaused || document.hidden) return;
  statusPending = true;
  const before = JSON.stringify(state.status);
  const errorBefore = state.errors.get("status");
  try {
    await loadResource("status");
    if (before === JSON.stringify(state.status) && errorBefore === state.errors.get("status")) return;
    const firstStatus = JSON.parse(before);
    const knownCompleted = firstStatus.jobs.filter((job) => !activeStates.has(job.state));
    const newlyCompleted = state.status.jobs.some((job) => !activeStates.has(job.state)
      && !knownCompleted.some((previous) => previous.id === job.id && previous.action === job.action && previous.state === job.state));
    if (newlyCompleted) {
      await Promise.all(["library", "compatibility"].map(loadResource));
      renderLibrary();
      renderCatalogMatches();
      if (detailJobWasRemoved(knownCompleted)) {
        dialog.close();
      }
    }
    renderStatus();
    renderDiagnostics();
    const priorDetailJob = firstStatus.jobs.find((job) => job.id === state.detailId);
    const detailJob = jobFor(state.detailId);
    if (priorDetailJob?.state !== detailJob?.state || priorDetailJob?.error !== detailJob?.error) renderDetail();
  } finally {
    statusPending = false;
  }
}

function detailJobWasRemoved(knownCompleted) {
  if (!dialog.open) return false;
  const job = jobFor(state.detailId);
  return job?.action === "uninstall" && job.state === "completed"
    && !knownCompleted.some((previous) => previous.id === job.id && previous.action === job.action && previous.state === job.state);
}

async function runAction(action, id) {
  if (state.action) return;
  state.action = action;
  state.actionError = null;
  pollingPaused = true;
  state.confirmingUninstall = false;
  renderDetail();
  const title = gameTitle(id);
  try {
    const result = await request(action, { id });
    if (result.state) {
      state.status.jobs = [result, ...state.status.jobs.filter((job) => job.id !== id)];
      announce((action === "install" ? "Installation" : "Deinstallation") + " beauftragt. Fortschritt unter Downloads.");
    }
  } catch (error) {
    announce(error.message);
    if (state.detailId === id) state.actionError = error.message;
  } finally {
    state.action = null;
    await refresh();
    if (action === "uninstall" && state.loaded.has("library") && !state.errors.has("library") && !installedGame(id)) {
      if (dialog.open) dialog.close();
      announce(title + " wurde entfernt.");
    }
    pollingPaused = false;
  }
}

async function syncCatalog() {
  if (state.syncing) return;
  state.syncing = true;
  renderCatalog();
  try {
    const result = await request("sync", {});
    if (result.state === "syncing") {
      await pollCatalog();
      return;
    }
    await loadResource("catalog");
    finishSync();
  } catch (error) {
    announce(error.message);
    state.syncing = false;
    renderCatalog();
    renderDiagnostics();
  }
}

async function pollCatalog() {
  await loadResource("catalog");
  if (state.errors.has("catalog") || !state.catalog.sources.some((source) => source.state === "syncing")) {
    finishSync();
    return;
  }
  renderDiagnostics();
  setTimeout(pollCatalog, 1200);
}

navigation.addEventListener("click", (event) => {
  const button = event.target.closest("[data-view]");
  if (!button) return;
  setView(button.dataset.view);
  if (button.dataset.view === "diagnostics") loadResource("compatibility").then(renderDiagnostics);
});
get("search-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (state.view !== "discover") setView("discover");
  renderCatalogMatches();
  get("catalog-grid").querySelector("button")?.focus();
});
search.addEventListener("input", () => {
  if (state.view !== "discover") setView("discover");
  renderCatalogMatches();
});
providerFilter.addEventListener("change", renderCatalogMatches);
availabilityFilter.addEventListener("change", renderCatalogMatches);
modeFilter.addEventListener("change", renderCatalogMatches);
get("hero-game").addEventListener("click", (event) => showDetail(event.currentTarget.dataset.game));
get("sync-button").addEventListener("click", syncCatalog);
get("detail-close").addEventListener("click", () => dialog.close());
dialog.addEventListener("close", () => {
  state.detailId = null;
  state.confirmingUninstall = false;
});

const focusable = "button:not([disabled]), a[href], input:not([disabled]), select:not([disabled])";
const directions = {
  ArrowUp: [0, -1], ArrowDown: [0, 1], ArrowLeft: [-1, 0], ArrowRight: [1, 0],
};

function navigate(direction) {
  const root = dialog.open ? dialog : document;
  const candidates = [...root.querySelectorAll(focusable)].filter((item) => item.getClientRects().length && !item.classList.contains("skip"));
  const active = document.activeElement;
  if (!candidates.includes(active)) {
    candidates[0]?.focus();
    return;
  }
  const center = active.getBoundingClientRect();
  const [horizontal, vertical] = directions[direction];
  let nearest = null;
  let score = Infinity;
  for (const candidate of candidates) {
    if (candidate === active) continue;
    const rect = candidate.getBoundingClientRect();
    const x = rect.left + rect.width / 2 - (center.left + center.width / 2);
    const y = rect.top + rect.height / 2 - (center.top + center.height / 2);
    const forward = x * horizontal + y * vertical;
    if (forward < 5) continue;
    const sideways = Math.abs(x * vertical + y * horizontal);
    const distance = Math.hypot(forward, sideways) + sideways * 2;
    if (distance < score) {
      score = distance;
      nearest = candidate;
    }
  }
  nearest?.focus();
}

function goBack() {
  if (dialog.open) dialog.close();
  else if (state.view !== "discover") setView("discover", true);
}

document.addEventListener("keydown", (event) => {
  if (event.ctrlKey || event.altKey || event.metaKey || event.shiftKey || event.defaultPrevented) return;
  if (directions[event.key] && !event.target.closest("input, select")) {
    event.preventDefault();
    navigate(event.key);
  } else if (event.key === "Escape" && !dialog.open && event.target !== search) {
    goBack();
  }
});
document.addEventListener("pointerdown", () => document.body.classList.remove("controller-focus"));

let padIndex = null;
let lastDirection = "";
let lastMotion = 0;
let previousPadButtons = [false, false];

function pollPad(time) {
  const pads = typeof navigator.getGamepads === "function" ? navigator.getGamepads() : [];
  const pad = Array.from(pads).find((item) => item?.connected && item.mapping === "standard");
  if ((pad?.index ?? null) !== padIndex) {
    padIndex = pad?.index ?? null;
    get("controller-state").textContent = pad ? "Controller verbunden" : "Tastatursteuerung";
    get("direction-hint").textContent = pad ? "Steuerkreuz" : "↑ ↓ ← →";
    get("confirm-hint").textContent = pad ? "× / A" : "Enter";
    get("back-hint").textContent = pad ? "○ / B" : "Esc";
    previousPadButtons = [Boolean(pad?.buttons[0]?.pressed), Boolean(pad?.buttons[1]?.pressed)];
    lastDirection = "";
  }
  if (pad) {
    const axisX = pad.axes[0] ?? 0;
    const axisY = pad.axes[1] ?? 0;
    const direction = Object.keys(directions).find((key, index) => {
      if (pad.buttons[[12, 13, 14, 15][index]]?.pressed) return true;
      if (Math.abs(axisX) > Math.abs(axisY)) return key === (axisX > 0 ? "ArrowRight" : "ArrowLeft") && Math.abs(axisX) >= .55;
      return key === (axisY > 0 ? "ArrowDown" : "ArrowUp") && Math.abs(axisY) >= .55;
    });
    if (direction && (direction !== lastDirection || time - lastMotion > 260)) {
      document.body.classList.add("controller-focus");
      navigate(direction);
      lastMotion = time;
    }
    lastDirection = direction;
    const pressed = [Boolean(pad.buttons[0]?.pressed), Boolean(pad.buttons[1]?.pressed)];
    if (pressed[0] && !previousPadButtons[0]) {
      const active = document.activeElement;
      if (active?.matches("button:not([disabled])")) active.click();
      else if (active?.matches("a[href]")) window.location.assign(active.href);
      else if (active?.matches("select")) {
        active.selectedIndex = (active.selectedIndex + 1) % active.options.length;
        active.dispatchEvent(new Event("change"));
      }
    }
    if (pressed[1] && !previousPadButtons[1]) goBack();
    previousPadButtons = pressed;
  }
  requestAnimationFrame(pollPad);
}

requestAnimationFrame(pollPad);
renderCatalog();
refresh().then(() => setInterval(pollStatus, 1500));

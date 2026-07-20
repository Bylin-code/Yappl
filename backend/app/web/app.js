const state = { sessions: [], memory: null, selectedDate: null, memoryCategory: "person", yearOffset: 0 };
const $ = (selector) => document.querySelector(selector);

function localDateKey(epoch) {
  const date = new Date(epoch * 1000);
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
}

function dateKey(date) {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
}

function sessionsByDay() {
  const days = new Map();
  state.sessions.forEach((session) => {
    if (!session.completed_at_epoch) return;
    const key = localDateKey(session.completed_at_epoch);
    if (!days.has(key)) days.set(key, []);
    days.get(key).push(session);
  });
  return days;
}

function intensity(minutes, maxMinutes) {
  if (!minutes) return 0;
  const ratio = minutes / Math.max(maxMinutes, 1);
  if (ratio <= .25) return 1;
  if (ratio <= .5) return 2;
  if (ratio <= .75) return 3;
  return 4;
}

function buildHeatmap() {
  const heatmap = $("#heatmap");
  const months = $("#month-labels");
  heatmap.innerHTML = "";
  months.innerHTML = "";
  const grouped = sessionsByDay();
  const totals = [...grouped.values()].map((items) => items.reduce((sum, item) => sum + item.duration_seconds / 60, 0));
  const maxMinutes = Math.max(...totals, 1);
  const end = new Date();
  end.setHours(0, 0, 0, 0);
  end.setFullYear(end.getFullYear() - state.yearOffset);
  const start = new Date(end);
  start.setDate(start.getDate() - 364 - start.getDay());
  let lastMonth = start.getMonth();
  // A rolling year usually begins and ends in the same month. In that case,
  // show only the current/end month so the partial starting month is not duplicated.
  if (start.getMonth() !== end.getMonth()) {
    const firstMonthLabel = document.createElement("span");
    firstMonthLabel.textContent = start.toLocaleDateString(undefined, { month: "short" });
    firstMonthLabel.style.gridColumn = "1 / span 4";
    months.appendChild(firstMonthLabel);
  }

  for (let i = 0; i < 371; i += 1) {
    const date = new Date(start);
    date.setDate(start.getDate() + i);
    const key = dateKey(date);
    const entries = grouped.get(key) || [];
    const minutes = entries.reduce((sum, entry) => sum + entry.duration_seconds / 60, 0);
    const cell = document.createElement("button");
    cell.className = `day-cell level-${intensity(minutes, maxMinutes)}`;
    cell.dataset.date = key;
    cell.setAttribute("role", "gridcell");
    const spoken = minutes ? `${Math.round(minutes)} minutes yapped` : "No yap";
    cell.setAttribute("aria-label", `${date.toLocaleDateString(undefined, { dateStyle: "long" })}: ${spoken}`);
    if (date > end) { cell.disabled = true; cell.style.opacity = "0"; }
    cell.addEventListener("mouseenter", (event) => showTooltip(event, date, entries, minutes));
    cell.addEventListener("mousemove", positionTooltip);
    cell.addEventListener("mouseleave", hideTooltip);
    cell.addEventListener("focus", (event) => showTooltip(event, date, entries, minutes));
    cell.addEventListener("blur", hideTooltip);
    cell.addEventListener("click", () => selectDay(key));
    heatmap.appendChild(cell);

    if (date.getDate() === 1 && date.getMonth() !== lastMonth) {
      const label = document.createElement("span");
      label.textContent = date.toLocaleDateString(undefined, { month: "short" });
      label.style.gridColumn = `${Math.floor(i / 7) + 1} / span 4`;
      months.appendChild(label);
      lastMonth = date.getMonth();
    }
  }
  const rangeOptions = { month: "short", year: "numeric" };
  $("#heatmap-range").textContent = `${start.toLocaleDateString(undefined, rangeOptions)} – ${end.toLocaleDateString(undefined, rangeOptions)}`;
  $("#newer-year").disabled = state.yearOffset === 0;
  $("#total-yaps").textContent = state.sessions.length;
}

function showTooltip(event, date, entries, minutes) {
  const tooltip = $("#tooltip");
  const previews = entries.map((entry) => entry.summary_excerpt).filter(Boolean);
  const excerpt = previews.length ? previews.slice(0, 2).join(" ") : "No journal entry";
  tooltip.innerHTML = `<strong>${date.toLocaleDateString(undefined, { month: "short", day: "numeric", year: "numeric" })} · ${minutes ? `${Math.round(minutes)} min` : "No yap"}</strong>${excerpt}`;
  tooltip.hidden = false;
  positionTooltip(event);
}

function positionTooltip(event) {
  const tooltip = $("#tooltip");
  if (tooltip.hidden) return;
  const x = event.clientX || event.target.getBoundingClientRect().left;
  const y = event.clientY || event.target.getBoundingClientRect().top;
  tooltip.style.left = `${Math.min(x + 14, window.innerWidth - tooltip.offsetWidth - 10)}px`;
  tooltip.style.top = `${Math.max(8, y - tooltip.offsetHeight - 12)}px`;
}
function hideTooltip() { $("#tooltip").hidden = true; }

function selectDay(key) {
  state.selectedDate = key;
  document.querySelectorAll(".day-cell").forEach((cell) => cell.classList.toggle("selected", cell.dataset.date === key));
  const entries = sessionsByDay().get(key) || [];
  const panel = $("#day-detail");
  const date = new Date(`${key}T12:00:00`);
  panel.classList.remove("empty");
  const totalMinutes = Math.round(entries.reduce((sum, entry) => sum + entry.duration_seconds / 60, 0));
  panel.innerHTML = `<div class="detail-heading"><h2>${date.toLocaleDateString(undefined, { weekday: "long", month: "long", day: "numeric" })}</h2><span>${entries.length ? `${entries.length} ${entries.length === 1 ? "session" : "sessions"} · ${totalMinutes} min` : "No yap recorded"}</span></div>`;
  if (!entries.length) {
    panel.innerHTML += `<div class="entry"><p class="no-summary">There is no journal entry for this day.</p></div>`;
    return;
  }
  entries.forEach((entry, index) => {
    const time = new Date(entry.completed_at_epoch * 1000).toLocaleTimeString(undefined, { hour: "numeric", minute: "2-digit" });
    const transcript = entry.transcript || "Corrected transcript is not available yet.";
    const audio = entry.audio_url ? `<div class="recording"><span>Original audio</span><audio controls preload="metadata" src="${escapeHtml(entry.audio_url)}"></audio></div>` : `<p class="audio-unavailable">Original audio is not available.</p>`;
    panel.innerHTML += `<article class="entry" data-session-id="${escapeHtml(entry.session_id)}"><div class="entry-meta"><span>${time}</span><i></i><span>${Math.round(entry.duration_seconds / 60)} minutes</span>${entries.length > 1 ? `<i></i><span>Session ${index + 1}</span>` : ""}</div><div class="entry-view-toggle" role="tablist" aria-label="Journal entry view"><button class="active" type="button" data-entry-view="summary" role="tab" aria-selected="true">Summary</button><button type="button" data-entry-view="transcript" role="tab" aria-selected="false">Corrected transcript</button></div><div class="entry-body" data-entry-panel="summary"><p class="${entry.summary ? "" : "no-summary"}">${escapeHtml(entry.summary || "Summary is not available yet.")}</p></div><div class="entry-body transcript-body" data-entry-panel="transcript" hidden>${audio}<p class="${entry.transcript ? "" : "no-summary"}">${escapeHtml(transcript)}</p></div></article>`;
  });
  panel.querySelectorAll(".entry-view-toggle button").forEach((button) => button.addEventListener("click", () => {
    const entry = button.closest(".entry");
    const view = button.dataset.entryView;
    entry.querySelectorAll(".entry-view-toggle button").forEach((tab) => {
      const active = tab === button;
      tab.classList.toggle("active", active);
      tab.setAttribute("aria-selected", active);
    });
    entry.querySelectorAll("[data-entry-panel]").forEach((body) => { body.hidden = body.dataset.entryPanel !== view; });
  }));
  panel.scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[char]);
}

const categories = [
  ["person", "People"], ["place", "Places"], ["object", "Objects"], ["event", "Events"],
  ["organization", "Organizations"], ["project", "Projects"], ["pending", "Pending"]
];

function buildMemoryTabs() {
  const tabs = $("#memory-tabs");
  tabs.innerHTML = "";
  categories.forEach(([id, label]) => {
    const button = document.createElement("button");
    button.className = `memory-tab ${state.memoryCategory === id ? "active" : ""}`;
    button.textContent = label;
    button.setAttribute("role", "tab");
    button.setAttribute("aria-selected", state.memoryCategory === id);
    button.addEventListener("click", () => { state.memoryCategory = id; buildMemoryTabs(); renderMemory(); });
    tabs.appendChild(button);
  });
}

function renderMemory() {
  const content = $("#memory-content");
  if (!state.memory) { content.innerHTML = `<p class="no-summary">Loading memory…</p>`; return; }
  if (state.memoryCategory === "pending") {
    const items = state.memory.pending_entities;
    content.innerHTML = items.length ? `<div class="memory-grid">${items.map((item) => `<article class="memory-card"><h3>${escapeHtml(item.canonical_name)}</h3><p>${escapeHtml(item.description)} · ${item.mention_count} mention</p></article>`).join("")}</div>` : `<p class="no-summary">No memories are waiting for another mention.</p>`;
    return;
  }
  const items = state.memory.categories[state.memoryCategory] || [];
  content.innerHTML = items.length ? `<div class="memory-grid">${items.map((item) => `<button class="memory-card" data-memory-id="${escapeHtml(item.id)}"><h3>${escapeHtml(item.canonical_name)}</h3><p>${escapeHtml(item.description || "No description yet.")}</p></button>`).join("")}</div>` : `<p class="no-summary">No confirmed ${state.memoryCategory}s yet. Repeated mentions will appear here.</p>`;
  content.querySelectorAll("[data-memory-id]").forEach((button) => button.addEventListener("click", () => showMemoryDetail(items.find((item) => item.id === button.dataset.memoryId))));
}

function showMemoryDetail(item) {
  const aliases = item.aliases?.map((alias) => alias.alias).filter((alias) => alias.toLowerCase() !== item.canonical_name.toLowerCase()) || [];
  const facts = item.facts || [];
  $("#memory-content").innerHTML = `<article class="memory-detail"><button class="back-button">← Back to ${state.memoryCategory}s</button><h2>${escapeHtml(item.canonical_name)}</h2><p class="description">${escapeHtml(item.description || "No description yet.")}</p>${aliases.length ? `<p class="description"><strong>Also known as:</strong> ${aliases.map(escapeHtml).join(", ")}</p>` : ""}<h3>What Yappl remembers</h3><ul class="fact-list">${facts.length ? facts.map((fact) => `<li><strong>${escapeHtml(fact.predicate.replaceAll("_", " "))}</strong><span>${escapeHtml(fact.value)}</span></li>`).join("") : `<li><span>No additional details yet.</span></li>`}</ul></article>`;
  $(".back-button").addEventListener("click", renderMemory);
}

function switchView(view) {
  document.querySelectorAll(".view").forEach((section) => section.classList.toggle("active", section.id === `${view}-view`));
  document.querySelectorAll(".nav-link").forEach((button) => button.classList.toggle("active", button.dataset.view === view));
  history.replaceState(null, "", `#${view}`);
}

async function load() {
  try {
    const [sessionResponse, memoryResponse] = await Promise.all([fetch("/api/journal/sessions"), fetch("/api/memory/library")]);
    if (!sessionResponse.ok || !memoryResponse.ok) throw new Error("Unable to load Yappl data");
    state.sessions = (await sessionResponse.json()).sessions;
    state.memory = await memoryResponse.json();
    buildHeatmap(); buildMemoryTabs(); renderMemory();
    const latest = [...state.sessions].reverse().find((session) => session.completed_at_epoch);
    if (latest) selectDay(localDateKey(latest.completed_at_epoch));
  } catch (error) {
    $("#day-detail").innerHTML = `<div class="empty-state"><p>${escapeHtml(error.message)}</p></div>`;
    $("#memory-content").innerHTML = `<p class="no-summary">${escapeHtml(error.message)}</p>`;
  }
}

document.querySelectorAll(".nav-link").forEach((button) => button.addEventListener("click", () => switchView(button.dataset.view)));
$("#older-year").addEventListener("click", () => { state.yearOffset += 1; buildHeatmap(); });
$("#newer-year").addEventListener("click", () => { if (state.yearOffset > 0) state.yearOffset -= 1; buildHeatmap(); });
switchView(location.hash === "#memory" ? "memory" : "journal");
load();

const state = { sessions: [], memory: null, selectedDate: null, memoryCategory: "person", yearOffset: 0, journalPeriod: "daily", periodSummaries: {} };
const $ = (selector) => document.querySelector(selector);

function localDateKey(epoch) {
  const date = new Date(epoch * 1000);
  // Journal days roll over at 8 AM local time, so late-night sessions after
  // midnight still appear on the previous day's heatmap square.
  date.setHours(date.getHours() - 8);
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

function readablePeriodDate(value, includeYear = true) {
  return new Date(`${value}T12:00:00`).toLocaleDateString(undefined, { month: "short", day: "numeric", ...(includeYear ? { year: "numeric" } : {}) });
}

function periodTitle(periodType, item) {
  if (periodType === "weekly") return `${readablePeriodDate(item.period_start, false)} – ${readablePeriodDate(item.period_end)}`;
  if (periodType === "monthly") return new Date(`${item.period_start}T12:00:00`).toLocaleDateString(undefined, { month: "long", year: "numeric" });
  return new Date(`${item.period_start}T12:00:00`).getFullYear().toString();
}

function renderPeriodSummaries(periodType, items) {
  const container = $("#period-summaries");
  if (!items.length) {
    container.innerHTML = `<div class="period-loading">No completed ${periodType === "weekly" ? "weeks" : periodType === "monthly" ? "months" : "years"} with journal entries yet.</div>`;
    return;
  }
  container.innerHTML = `<div class="period-grid">${items.map((item) => `<button class="period-preview-card" type="button" data-period-id="${escapeHtml(item.id)}"><span class="period-preview-label">${periodType === "weekly" ? "Week" : periodType === "monthly" ? "Month" : "Year"}</span><h2>${escapeHtml(periodTitle(periodType, item))}</h2><p>${escapeHtml(item.summary_excerpt || item.error || "Summary is not available.")}</p><small>${item.session_count} ${item.session_count === 1 ? "session" : "sessions"} · ${Math.round(item.duration_seconds / 60)} min</small></button>`).join("")}</div>`;
  container.querySelectorAll("[data-period-id]").forEach((button) => button.addEventListener("click", () => renderPeriodDetail(periodType, items.find((item) => item.id === button.dataset.periodId))));
}

function renderPeriodDetail(periodType, item) {
  const container = $("#period-summaries");
  const sessionLinks = item.session_ids.map((sessionId) => {
    const session = state.sessions.find((candidate) => candidate.session_id === sessionId);
    if (!session) return "";
    const key = localDateKey(session.completed_at_epoch);
    const label = new Date(`${key}T12:00:00`).toLocaleDateString(undefined, { weekday: "short", month: "short", day: "numeric", year: periodType === "yearly" ? "numeric" : undefined });
    return `<button type="button" data-period-day="${key}">${label}</button>`;
  }).join("");
  container.innerHTML = `<article class="period-detail"><button class="back-button period-back" type="button">← Back to ${periodType === "weekly" ? "weeks" : periodType === "monthly" ? "months" : "years"}</button><div class="period-detail-heading"><div><span>${periodType === "weekly" ? "Weekly" : periodType === "monthly" ? "Monthly" : "Yearly"} reflection</span><h2>${escapeHtml(periodTitle(periodType, item))}</h2></div><small>${item.session_count} ${item.session_count === 1 ? "session" : "sessions"} · ${Math.round(item.duration_seconds / 60)} min</small></div><p class="period-full-summary ${item.status === "complete" ? "" : "no-summary"}">${escapeHtml(item.summary || item.error || "Summary is not available.")}</p><div class="period-days"><span>Included days</span>${sessionLinks}</div></article>`;
  container.querySelector(".period-back").addEventListener("click", () => renderPeriodSummaries(periodType, state.periodSummaries[periodType]));
  container.querySelectorAll("[data-period-day]").forEach((button) => button.addEventListener("click", () => {
    switchJournalPeriod("daily");
    selectDay(button.dataset.periodDay);
  }));
}

async function switchJournalPeriod(periodType) {
  state.journalPeriod = periodType;
  document.querySelectorAll("[data-journal-period]").forEach((button) => {
    const active = button.dataset.journalPeriod === periodType;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", active);
  });
  const daily = periodType === "daily";
  $("#daily-heatmap").hidden = !daily;
  $("#day-detail").hidden = !daily;
  $("#period-summaries").hidden = daily;
  $("#journal-title").textContent = daily ? "A year in yaps" : periodType === "weekly" ? "Weeks in review" : periodType === "monthly" ? "Months in review" : "Years in review";
  if (daily) return;
  const container = $("#period-summaries");
  if (state.periodSummaries[periodType]) {
    renderPeriodSummaries(periodType, state.periodSummaries[periodType]);
    return;
  }
  container.innerHTML = `<div class="period-loading">Building ${periodType} summaries…</div>`;
  try {
    const response = await fetch(`/api/journal/period-summaries?period_type=${periodType}`);
    const result = await response.json();
    if (!response.ok) throw new Error(result.detail || "Unable to load summaries");
    state.periodSummaries[periodType] = result.summaries;
    if (state.journalPeriod === periodType) renderPeriodSummaries(periodType, result.summaries);
  } catch (error) {
    container.innerHTML = `<div class="period-loading">${escapeHtml(error.message)}</div>`;
  }
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
    content.innerHTML = items.length ? `<div class="memory-grid">${items.map((item) => `<button class="memory-card" data-pending-key="${escapeHtml(item.normalized_key)}"><h3>${escapeHtml(item.canonical_name)}</h3><p>${escapeHtml(item.description)} · Recommended: ${escapeHtml(item.type)} · ${item.mention_count} mention</p></button>`).join("")}</div>` : `<p class="no-summary">No memories are waiting for another mention.</p>`;
    content.querySelectorAll("[data-pending-key]").forEach((button) => button.addEventListener("click", () => {
      const pending = items.find((item) => item.normalized_key === button.dataset.pendingKey);
      let aliases = [];
      let facts = [];
      try { aliases = JSON.parse(pending.aliases_json || "[]").map((alias) => ({ alias })); } catch (_) { aliases = []; }
      try { facts = JSON.parse(pending.facts_json || "[]"); } catch (_) { facts = []; }
      renderMemoryForm({ type: pending.type, canonical_name: pending.canonical_name, description: pending.description, aliases, facts }, pending.normalized_key);
    }));
    return;
  }
  const items = state.memory.categories[state.memoryCategory] || [];
  content.innerHTML = items.length ? `<div class="memory-grid">${items.map((item) => `<button class="memory-card" data-memory-id="${escapeHtml(item.id)}"><h3>${escapeHtml(item.canonical_name)}</h3><p>${escapeHtml(item.description || "No description yet.")}</p></button>`).join("")}</div>` : `<p class="no-summary">No confirmed ${state.memoryCategory}s yet. Repeated mentions will appear here.</p>`;
  content.querySelectorAll("[data-memory-id]").forEach((button) => button.addEventListener("click", () => showMemoryDetail(items.find((item) => item.id === button.dataset.memoryId))));
}

function showMemoryDetail(item) {
  const aliases = item.aliases?.map((alias) => alias.alias).filter((alias) => alias.toLowerCase() !== item.canonical_name.toLowerCase()) || [];
  const facts = item.facts || [];
  $("#memory-content").innerHTML = `<article class="memory-detail"><div class="memory-detail-actions"><button class="memory-secondary edit-memory" type="button">Edit</button><button class="memory-danger delete-memory" type="button">Delete</button></div><button class="back-button">← Back to ${state.memoryCategory}s</button><h2>${escapeHtml(item.canonical_name)}</h2><p class="description">${escapeHtml(item.description || "No description yet.")}</p>${aliases.length ? `<p class="description"><strong>Also known as:</strong> ${aliases.map(escapeHtml).join(", ")}</p>` : ""}<h3>What Yappl remembers</h3><ul class="fact-list">${facts.length ? facts.map((fact) => `<li><strong>${escapeHtml(fact.predicate.replaceAll("_", " "))}</strong><span>${escapeHtml(fact.value)}</span></li>`).join("") : `<li><span>No additional details yet.</span></li>`}</ul></article>`;
  $(".back-button").addEventListener("click", renderMemory);
  $(".edit-memory").addEventListener("click", () => renderMemoryForm(item));
  $(".delete-memory").addEventListener("click", async () => {
    if (!window.confirm(`Delete ${item.canonical_name} and all of its remembered information?`)) return;
    try {
      const response = await fetch(`/api/memory/entities/${encodeURIComponent(item.id)}`, { method: "DELETE" });
      if (!response.ok) throw new Error((await response.json()).detail || "Unable to delete memory");
      await refreshMemory();
    } catch (error) { window.alert(error.message); }
  });
}

function factEditorRow(fact = {}) {
  return `<div class="fact-editor-row"><input name="predicate" value="${escapeHtml(fact.predicate || "")}" placeholder="Relationship, job, usual activity…" aria-label="Fact label"><input name="value" value="${escapeHtml(fact.value || "")}" placeholder="What should Yappl remember?" aria-label="Fact value"><button class="remove-fact" type="button" aria-label="Remove fact">×</button></div>`;
}

function wireFactRows(form) {
  form.querySelectorAll(".remove-fact").forEach((button) => button.addEventListener("click", () => button.closest(".fact-editor-row").remove()));
}

function renderMemoryForm(item = null, pendingKey = null) {
  const category = item?.type || (state.memoryCategory === "pending" ? "person" : state.memoryCategory);
  const aliases = item?.aliases?.map((alias) => alias.alias).filter((alias) => alias.toLowerCase() !== item.canonical_name.toLowerCase()) || [];
  const facts = item?.facts || [];
  const options = categories.filter(([id]) => id !== "pending").map(([id, label]) => `<option value="${id}" ${id === category ? "selected" : ""}>${label}</option>`).join("");
  $("#memory-content").innerHTML = `<form id="memory-editor" class="memory-editor"><h2>${pendingKey ? "Review pending memory" : item ? "Edit memory" : "Add memory"}</h2>${pendingKey ? `<p class="description">Review the suggested information and category before adding it to your memory library.</p>` : ""}<div class="memory-form-grid"><div class="memory-field"><label for="memory-name">Name</label><input id="memory-name" name="name" value="${escapeHtml(item?.canonical_name || "")}" required maxlength="120" autofocus></div><div class="memory-field"><label for="memory-type">${pendingKey ? "Recommended category" : "Category"}</label><select id="memory-type" name="type">${options}</select></div><div class="memory-field full"><label for="memory-description">Description</label><textarea id="memory-description" name="description" maxlength="1000" placeholder="A readable overview of who or what this is…">${escapeHtml(item?.description || "")}</textarea></div><div class="memory-field full"><label for="memory-aliases">Other names or spellings</label><textarea id="memory-aliases" name="aliases" placeholder="One per line, or separated by commas">${escapeHtml(aliases.join("\n"))}</textarea></div></div><div class="facts-editor"><label>Remembered facts</label><div class="fact-editor-rows">${facts.map(factEditorRow).join("")}</div><button class="add-fact" type="button">+ Add fact</button></div><p class="form-error" hidden></p><div class="memory-form-actions">${item?.id && !pendingKey ? `<button class="memory-danger delete-from-editor" type="button">Delete</button>` : ""}<span class="memory-form-spacer"></span><button class="memory-secondary cancel-memory" type="button">Cancel</button><button class="memory-primary" type="submit">${pendingKey ? "Add to library" : "Save memory"}</button></div></form>`;
  const form = $("#memory-editor");
  wireFactRows(form);
  form.querySelector(".add-fact").addEventListener("click", () => {
    form.querySelector(".fact-editor-rows").insertAdjacentHTML("beforeend", factEditorRow());
    wireFactRows(form);
  });
  const submit = form.querySelector("[type=submit]");
  const updatePromotionLabel = () => {
    if (!pendingKey) return;
    const selected = categories.find(([id]) => id === form.elements.type.value);
    submit.textContent = `Add to ${selected ? selected[1] : "library"}`;
  };
  form.elements.type.addEventListener("change", updatePromotionLabel);
  updatePromotionLabel();
  form.querySelector(".delete-from-editor")?.addEventListener("click", async () => {
    if (!window.confirm(`Delete ${item.canonical_name} and all of its remembered information?`)) return;
    try {
      const response = await fetch(`/api/memory/entities/${encodeURIComponent(item.id)}`, { method: "DELETE" });
      if (!response.ok) throw new Error((await response.json()).detail || "Unable to delete memory");
      await refreshMemory();
    } catch (problem) {
      const error = form.querySelector(".form-error");
      error.textContent = problem.message;
      error.hidden = false;
    }
  });
  form.querySelector(".cancel-memory").addEventListener("click", () => pendingKey ? renderMemory() : item ? showMemoryDetail(item) : renderMemory());
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const error = form.querySelector(".form-error");
    submit.disabled = true;
    error.hidden = true;
    const factValues = [...form.querySelectorAll(".fact-editor-row")].map((row) => ({ predicate: row.querySelector("[name=predicate]").value.trim().replaceAll(" ", "_"), value: row.querySelector("[name=value]").value.trim() })).filter((fact) => fact.predicate && fact.value);
    const payload = { id: item?.id || null, type: form.elements.type.value, canonical_name: form.elements.name.value.trim(), description: form.elements.description.value.trim(), aliases: form.elements.aliases.value.split(/[\n,]+/).map((value) => value.trim()).filter(Boolean), facts: factValues };
    try {
      const url = pendingKey ? `/api/memory/pending/${encodeURIComponent(pendingKey)}/promote` : item ? `/api/memory/entities/${encodeURIComponent(item.id)}` : "/api/memory/entities";
      const response = await fetch(url, { method: pendingKey ? "POST" : item ? "PUT" : "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.detail || "Unable to save memory");
      state.memoryCategory = result.entity.type;
      await refreshMemory(result.entity.id);
    } catch (problem) {
      error.textContent = problem.message;
      error.hidden = false;
      submit.disabled = false;
    }
  });
}

async function refreshMemory(showEntityId = null) {
  const response = await fetch("/api/memory/library");
  if (!response.ok) throw new Error("Unable to refresh memory");
  state.memory = await response.json();
  buildMemoryTabs();
  const items = state.memory.categories[state.memoryCategory] || [];
  const entity = showEntityId ? items.find((candidate) => candidate.id === showEntityId) : null;
  if (entity) showMemoryDetail(entity); else renderMemory();
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
document.querySelectorAll("[data-journal-period]").forEach((button) => button.addEventListener("click", () => switchJournalPeriod(button.dataset.journalPeriod)));
$("#add-memory").addEventListener("click", () => renderMemoryForm());
$("#older-year").addEventListener("click", () => { state.yearOffset += 1; buildHeatmap(); });
$("#newer-year").addEventListener("click", () => { if (state.yearOffset > 0) state.yearOffset -= 1; buildHeatmap(); });
switchView(location.hash === "#memory" ? "memory" : "journal");
load();

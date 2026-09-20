(function () {
  const physiologyEl = document.getElementById("physiology");
  const sensorsEl = document.getElementById("sensors");
  const actuatorsEl = document.getElementById("actuators");
  const fuzzEl = document.getElementById("fuzz-scale");
  const contingencyEl = document.getElementById("contingency");
  const contingencyCountEl = document.getElementById("contingency-count");
  const spatialGridEl = document.getElementById("spatial-grid");
  const spatialCountEl = document.getElementById("spatial-count");
  let ws;
  let memoryPoll;

  function fetchMemoryTab() {
    fetchContingency();
    fetchSpatial();
  }

  document.querySelectorAll("nav button").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll("nav button").forEach((b) => b.classList.remove("active"));
      document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById("tab-" + btn.dataset.tab).classList.add("active");

      if (btn.dataset.tab === "memory") {
        fetchMemoryTab();
        memoryPoll = setInterval(fetchMemoryTab, 2000);
      } else if (memoryPoll) {
        clearInterval(memoryPoll);
        memoryPoll = null;
      }
    });
  });

  function renderContingency(data) {
    contingencyCountEl.textContent = data.count + " / " + data.capacity + " slots";
    contingencyEl.innerHTML = "";

    if (!data.entries || data.entries.length === 0) {
      contingencyEl.innerHTML = '<p class="empty-hint">Nothing learned yet — move an actuator to give it something to notice.</p>';
      return;
    }

    data.entries.forEach((e) => {
      const card = document.createElement("div");
      card.className = "entry";
      const driveClass = e.mean_drive_delta >= 0 ? "drive-good" : "drive-bad";
      const actuatorTags = e.actuators
        .map((c) => '<span class="tag ' + (c.delta > 0 ? "up" : "down") + '">' + c.name + "</span>")
        .join("") || '<span class="tag">none</span>';
      const sensorTags = e.sensors
        .map((c) => '<span class="tag ' + (c.delta > 0 ? "up" : "down") + '">' + c.name + "</span>")
        .join("") || '<span class="tag">none</span>';
      card.innerHTML =
        '<div class="entry-head">' +
        "<span>strength " + e.strength.toFixed(2) + " · age " + e.age + "</span>" +
        '<span class="' + driveClass + '">Δdrive ' + e.mean_drive_delta.toFixed(3) + "</span>" +
        "</div>" +
        '<div class="row"><strong>actuators</strong> ' + actuatorTags + "</div>" +
        '<div class="row"><strong>sensors</strong> ' + sensorTags + "</div>";
      contingencyEl.appendChild(card);
    });
  }

  function fetchContingency() {
    fetch("/api/contingency")
      .then((r) => r.json())
      .then(renderContingency)
      .catch(() => {});
  }

  function renderSpatial(data) {
    spatialCountEl.textContent = data.count + " / " + data.capacity + " places";
    spatialGridEl.innerHTML = "";

    // Normalize score against the strongest cell present so brightness is
    // relative, not tied to an arbitrary absolute scale.
    const maxAbsScore = Math.max(
      0.001,
      ...data.cells.filter((c) => c.occupied).map((c) => Math.abs(c.score || 0))
    );

    data.cells.forEach((c) => {
      const sq = document.createElement("div");
      sq.className = "spatial-cell" + (c.occupied ? " occupied" : "");
      if (c.occupied) {
        const norm = Math.max(0, (c.score || 0) / maxAbsScore);
        sq.style.background = "rgba(194, 58, 31, " + (0.15 + 0.65 * norm) + ")";
        sq.title = "visits " + c.visit_count + " · age " + c.age + " · score " + (c.score || 0).toFixed(3);
      }
      spatialGridEl.appendChild(sq);
    });
  }

  function fetchSpatial() {
    fetch("/api/spatial")
      .then((r) => r.json())
      .then(renderSpatial)
      .catch(() => {});
  }

  function renderChannels(data) {
    physiologyEl.innerHTML = "";
    (data.physiology || []).forEach((p) => {
      const row = document.createElement("div");
      row.className = "channel";
      row.innerHTML =
        '<span class="name">' + p.name + "</span>" +
        '<span class="bar"><span data-phys-bar="' + p.name + '" style="width:' + (p.value * 100) + '%"></span></span>' +
        '<span class="value" data-phys="' + p.name + '">' + p.value.toFixed(2) + "</span>" +
        '<span class="drive" data-phys-drive="' + p.name + '">d ' + p.drive.toFixed(2) + "</span>";
      physiologyEl.appendChild(row);
    });
    if (typeof data.fuzz_scale === "number") {
      fuzzEl.textContent = "fuzz " + data.fuzz_scale.toFixed(2);
    }

    sensorsEl.innerHTML = "";
    data.sensors.forEach((s) => {
      const row = document.createElement("div");
      row.className = "channel";
      row.innerHTML =
        '<span class="name">' + s.name + '</span>' +
        '<span class="value" data-sensor="' + s.name + '">' + s.value.toFixed(3) + "</span>";
      sensorsEl.appendChild(row);
    });

    actuatorsEl.innerHTML = "";
    data.actuators.forEach((a) => {
      const row = document.createElement("div");
      row.className = "channel";
      row.innerHTML =
        '<span class="name">' + a.name +
        ' <span class="tag manual" data-actuator-manual="' + a.name + '" style="display:' +
        (a.manual ? "inline-block" : "none") + '">M</span></span>' +
        '<input type="range" min="' + a.min + '" max="' + a.max +
        '" step="0.01" value="' + a.value + '" data-actuator="' + a.name + '">' +
        '<span class="value" data-actuator-value="' + a.name + '">' + a.value.toFixed(2) + "</span>";
      actuatorsEl.appendChild(row);
    });

    actuatorsEl.querySelectorAll('input[type="range"]').forEach((input) => {
      input.addEventListener("input", () => {
        const name = input.dataset.actuator;
        const value = parseFloat(input.value);
        actuatorsEl.querySelector('[data-actuator-value="' + name + '"]').textContent = value.toFixed(2);
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ actuator: name, value: value }));
        }
      });
    });
  }

  function applyTelemetry(data) {
    (data.physiology || []).forEach((p) => {
      const valueEl = physiologyEl.querySelector('[data-phys="' + p.name + '"]');
      const driveEl = physiologyEl.querySelector('[data-phys-drive="' + p.name + '"]');
      const barEl = physiologyEl.querySelector('[data-phys-bar="' + p.name + '"]');
      if (valueEl) valueEl.textContent = p.value.toFixed(2);
      if (driveEl) driveEl.textContent = "d " + p.drive.toFixed(2);
      if (barEl) barEl.style.width = p.value * 100 + "%";
    });
    if (typeof data.fuzz_scale === "number") {
      fuzzEl.textContent = "fuzz " + data.fuzz_scale.toFixed(2);
    }

    data.sensors.forEach((s) => {
      const el = sensorsEl.querySelector('[data-sensor="' + s.name + '"]');
      if (el) el.textContent = s.value.toFixed(3);
    });
    data.actuators.forEach((a) => {
      const input = actuatorsEl.querySelector('input[data-actuator="' + a.name + '"]');
      const valueEl = actuatorsEl.querySelector('[data-actuator-value="' + a.name + '"]');
      const manualEl = actuatorsEl.querySelector('[data-actuator-manual="' + a.name + '"]');
      if (manualEl) manualEl.style.display = a.manual ? "inline-block" : "none";
      // Don't fight the user while they're dragging a slider.
      if (document.activeElement === input) return;
      if (input) input.value = a.value;
      if (valueEl) valueEl.textContent = a.value.toFixed(2);
    });
  }

  function connectWs() {
    ws = new WebSocket("ws://" + location.host + "/ws");
    ws.onmessage = (evt) => applyTelemetry(JSON.parse(evt.data));
    ws.onclose = () => setTimeout(connectWs, 2000);
  }

  fetch("/api/channels")
    .then((r) => r.json())
    .then((data) => {
      renderChannels(data);
      connectWs();
    });

  document.getElementById("wifi-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const form = new FormData(e.target);
    const status = document.getElementById("wifi-status");
    fetch("/api/config/wifi", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ssid: form.get("ssid"), password: form.get("password") }),
    })
      .then((r) => r.json())
      .then(() => {
        status.textContent = "Saved. Restarting…";
      })
      .catch(() => {
        status.textContent = "Error saving config.";
      });
  });
})();

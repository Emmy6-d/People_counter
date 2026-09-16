async function getJson(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(await response.text());
    return response.json();
}

function fmtTime(value) {
    return new Date(value).toLocaleString();
}

function showMessage(message) {
    const messageElement = document.getElementById("app-message");
    messageElement.textContent = message;
    messageElement.hidden = false;
}

async function loadSensorStatus() {
    try {
        const status = await getJson("/api/status");
        const statusElement = document.getElementById("sensor-status");
        statusElement.textContent = status.status;
        statusElement.dataset.status = status.status;
    } catch (error) {
        console.error(error);
    }
}

async function loadDashboard() {
    try {
        const today = new Date().toISOString().slice(0, 10);
        const daily = await getJson(`/api/day?date=${today}`);
        document.getElementById("current-inside").textContent = daily.inside;
        document.getElementById("total-entered").textContent = daily.entered;
        document.getElementById("total-exited").textContent = daily.exited;
        document.getElementById("today").textContent = daily.total_events;
        document.getElementById("daily-summary").textContent =
            `${daily.entered} entered, ${daily.exited} exited, net change ${daily.net}.`;

        document.getElementById("recentBody").innerHTML = daily.events.map(e => `
            <tr>
                <td>${fmtTime(e.event_time)}</td>
                <td>${e.device_id}</td>
                <td>${e.sensor_sequence}</td>
                <td>${e.count_delta > 0 ? "+1 entered" : "-1 exited"}</td>
            </tr>
        `).join("");
    } catch (error) {
        console.error(error);
        showMessage("Dashboard data is temporarily unavailable. Retrying automatically.");
    }
}

loadDashboard();
loadSensorStatus();
setInterval(loadDashboard, 30000);
setInterval(loadSensorStatus, 1000);

let dailyChart;

async function getJson(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(await response.text());
    return response.json();
}

function fmtTime(value) {
    return new Date(value).toLocaleString();
}

async function loadDashboard() {
    try {
        const summary = await getJson("/api/summary");
        document.getElementById("current-inside").textContent = summary.current_inside;
        document.getElementById("total-entered").textContent = summary.total_entered;
        document.getElementById("total-exited").textContent = summary.total_exited;
        document.getElementById("today").textContent = summary.today;

        const daily = await getJson("/api/daily?days=30");

        if (dailyChart) dailyChart.destroy();

        dailyChart = new Chart(document.getElementById("dailyChart"), {
            type: "line",
            data: {
                labels: daily.map(x => x.day),
                datasets: [{
                    label: "Net inside change",
                    data: daily.map(x => x.net),
                    tension: 0.25,
                    fill: false
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false
            }
        });

        const events = await getJson("/api/recent-events?limit=10");
        document.getElementById("recentBody").innerHTML = events.map(e => `
            <tr>
                <td>${fmtTime(e.event_time)}</td>
                <td>${e.device_id}</td>
                <td>${e.sensor_sequence}</td>
                <td>${e.count_delta > 0 ? "+1 entered" : "-1 exited"}</td>
            </tr>
        `).join("");
    } catch (error) {
        console.error(error);
        alert("Dashboard error: " + error.message);
    }
}

loadDashboard();
setInterval(loadDashboard, 30000);

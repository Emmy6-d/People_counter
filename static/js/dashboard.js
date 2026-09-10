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
        document.getElementById("today").textContent = summary.today;
        document.getElementById("week").textContent = summary.week;
        document.getElementById("month").textContent = summary.month;
        document.getElementById("all-time").textContent = summary.all_time;

        const daily = await getJson("/api/daily?days=30");

        if (dailyChart) dailyChart.destroy();

        dailyChart = new Chart(document.getElementById("dailyChart"), {
            type: "line",
            data: {
                labels: daily.map(x => x.day),
                datasets: [{
                    label: "People",
                    data: daily.map(x => x.people),
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
                <td>${e.count_delta}</td>
            </tr>
        `).join("");
    } catch (error) {
        console.error(error);
        alert("Dashboard error: " + error.message);
    }
}

loadDashboard();
setInterval(loadDashboard, 30000);

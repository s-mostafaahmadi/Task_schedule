#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include "wifi_config.h"


#define EEPROM_SIZE 4096
#define MAX_TASKS 30
#define MAX_PLANS 5
#define PLAN_NAME_SIZE 16
#define TIME_UPDATE_INTERVAL 1800000 // 30 minutes in milliseconds
#define TIME_CHECK_INTERVAL 10000   // 10 seconds in milliseconds


WebServer server(80);

struct Task {
  char name[32];
  int startHour;
  int startMinute;
  int durationMin;
};

struct Plan {
  char name[PLAN_NAME_SIZE];
  Task tasks[MAX_TASKS];
  int taskCount;
};

Plan plans[MAX_PLANS];
int currentPlanIndex = 0;
int planCount = 1;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 12600;
const int daylightOffset_sec = 0;

unsigned long lastTimeUpdate = 0;
unsigned long lastTimeCheck = 0;
time_t simulatedSeconds = 0;

void savePlansToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, 'P');
  EEPROM.put(1, planCount);
  EEPROM.put(5, currentPlanIndex);
  int addr = 9;
  for (int i = 0; i < planCount; i++) {
    EEPROM.put(addr, plans[i].name);
    addr += PLAN_NAME_SIZE;
    EEPROM.put(addr, plans[i].taskCount);
    addr += sizeof(int);
    for (int j = 0; j < plans[i].taskCount; j++) {
      EEPROM.put(addr, plans[i].tasks[j]);
      addr += sizeof(Task);
    }
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadPlansFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(0) == 'P') {
    EEPROM.get(1, planCount);
    if (planCount > MAX_PLANS) planCount = MAX_PLANS;
    EEPROM.get(5, currentPlanIndex);
    if (currentPlanIndex >= planCount) currentPlanIndex = 0;
    int addr = 9;
    for (int i = 0; i < planCount; i++) {
      EEPROM.get(addr, plans[i].name);
      plans[i].name[PLAN_NAME_SIZE - 1] = 0;
      addr += PLAN_NAME_SIZE;
      EEPROM.get(addr, plans[i].taskCount);
      if (plans[i].taskCount > MAX_TASKS) plans[i].taskCount = MAX_TASKS;
      addr += sizeof(int);
      for (int j = 0; j < plans[i].taskCount; j++) {
        EEPROM.get(addr, plans[i].tasks[j]);
        plans[i].tasks[j].name[sizeof(plans[i].tasks[j].name) - 1] = 0;
        addr += sizeof(Task);
      }
    }
  } else {
    planCount = 1;
    currentPlanIndex = 0;
    strcpy(plans[0].name, "Default Plan");
    plans[0].taskCount = 12;
    Task defaultTasks[12] = {
      {"Wake Up", 6, 30, 30},
      {"Breakfast", 7, 0, 30},
      {"Study", 7, 30, 120},
      {"Break", 9, 30, 30},
      {"Workout", 10, 0, 60},
      {"Lunch", 12, 0, 60},
      {"Rest", 13, 0, 30},
      {"Programming", 13, 30, 180},
      {"Dinner", 18, 0, 60},
      {"Relax", 19, 0, 90},
      {"Read Book", 20, 30, 60},
      {"Sleep", 22, 0, 480}
    };
    for (int i = 0; i < plans[0].taskCount; i++) {
      plans[0].tasks[i] = defaultTasks[i];
    }
    savePlansToEEPROM();
  }
  EEPROM.end();
}

bool updateTimeFromNTP() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  simulatedSeconds = mktime(&timeinfo);
  lastTimeUpdate = millis();
  return true;
}

void updateSimulatedTime() {
  unsigned long currentMillis = millis();
  unsigned long elapsedMillis = (currentMillis >= lastTimeUpdate) ? 
                                (currentMillis - lastTimeUpdate) : 
                                (0xFFFFFFFF - lastTimeUpdate + currentMillis + 1);
  simulatedSeconds += elapsedMillis / 1000;
  lastTimeUpdate = currentMillis - (elapsedMillis % 1000);
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi: " + WiFi.localIP().toString());

  loadPlansFromEEPROM();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while (!updateTimeFromNTP()) {
    Serial.println("Failed to get time, retrying...");
    delay(1000);
  }
  Serial.println("Initial time synchronized");

  server.on("/", handleRoot);
  server.on("/edit", handleEdit);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/switchPlan", HTTP_POST, handleSwitchPlan);
  server.on("/deletePlan", handleDeletePlan);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
    if (updateTimeFromNTP()) {
      Serial.println("Time updated from NTP");
    } else {
      Serial.println("Failed to update time from NTP");
    }
  }

  if (currentMillis - lastTimeCheck >= TIME_CHECK_INTERVAL) {
    updateSimulatedTime();
    lastTimeCheck = currentMillis;
  }
}

String formatTime(int h, int m) {
  char buf[6];
  sprintf(buf, "%02d:%02d", h, m);
  return String(buf);
}

void handleRoot() {
  updateSimulatedTime();
  
  struct tm timeinfo;
  localtime_r(&simulatedSeconds, &timeinfo);
  
  int nowMin = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int nowSecInDay = nowMin * 60 + timeinfo.tm_sec;

  int activeIndex = -1;
  for (int i = 0; i < plans[currentPlanIndex].taskCount; i++) {
    int start = plans[currentPlanIndex].tasks[i].startHour * 60 + plans[currentPlanIndex].tasks[i].startMinute;
    int end = start + plans[currentPlanIndex].tasks[i].durationMin;

    if (end >= 24 * 60) {
      int endMod = end % (24 * 60);
      if (nowMin >= start || nowMin < endMod) {
        activeIndex = i;
        break;
      }
    } else {
      if (nowMin >= start && nowMin < end) {
        activeIndex = i;
        break;
      }
    }
  }

  String taskName;
  int remainingSec = 0;
  float progressPercent = 0;

  if (activeIndex != -1) {
    taskName = String(plans[currentPlanIndex].tasks[activeIndex].name);
    int startSec = (plans[currentPlanIndex].tasks[activeIndex].startHour * 60 + plans[currentPlanIndex].tasks[activeIndex].startMinute) * 60;
    int endSec = startSec + plans[currentPlanIndex].tasks[activeIndex].durationMin * 60;

    if (endSec >= 24 * 3600) {
      int endMod = endSec % (24 * 3600);
      if (nowSecInDay >= startSec) {
        remainingSec = (24 * 3600 - nowSecInDay) + endMod;
      } else {
        remainingSec = endMod - nowSecInDay;
      }
    } else {
      remainingSec = endSec - nowSecInDay;
    }

    if (remainingSec < 0) remainingSec = 0;
    progressPercent = ((plans[currentPlanIndex].tasks[activeIndex].durationMin * 60 - remainingSec) * 100.0) / (plans[currentPlanIndex].tasks[activeIndex].durationMin * 60);
  } else {
    taskName = "No Active Task";
    remainingSec = 0;
  }

  int hoursLeft = remainingSec / 3600;
  int minutesLeft = (remainingSec % 3600) / 60;
  int secondsLeft = remainingSec % 60;

  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", hoursLeft, minutesLeft, secondsLeft);

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <meta http-equiv='refresh' content='10'>
  <title>Cyberpunk Scheduler</title>
  <link href='https://fonts.googleapis.com/css2?family=Exo+2:wght@300;400;700&display=swap' rel='stylesheet'>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: linear-gradient(135deg, #0d0d2b, #1a1a4e, #3b1a6d, #ff007a);
      background-size: 400% 400%;
      animation: cyberGradient 12s ease infinite;
      color: #e0e0ff;
      font-family: 'Exo 2', sans-serif;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 20px;
      overflow-x: hidden;
    }
    @keyframes cyberGradient {
      0% { background-position: 0% 50%; }
      50% { background-position: 100% 50%; }
      100% { background-position: 0% 50%; }
    }
    .container { max-width: 1000px; width: 100%; }
    .current-task {
      background: rgba(0, 0, 0, 0.7);
      border-radius: 15px;
      padding: 20px;
      margin-bottom: 30px;
      box-shadow: 0 0 20px rgba(0, 255, 255, 0.5);
      text-align: center;
      transform: perspective(1000px) rotateX(5deg);
      transition: transform 0.5s ease;
    }
    .current-task:hover { transform: perspective(1000px) rotateX(0deg); }
    .task-name {
      font-size: 3rem;
      font-weight: 700;
      text-shadow: 0 0 10px #00f0ff, 0 0 20px #ff00ff;
      animation: neonGlow 1.5s ease-in-out infinite;
    }
    @keyframes neonGlow {
      0%, 100% { text-shadow: 0 0 10px #00f0ff, 0 0 20px #ff00ff; }
      50% { text-shadow: 0 0 20px #00f0ff, 0 0 30px #ff00ff; }
    }
    .timer {
      font-size: 2.5rem;
      color: #00f0ff;
      text-shadow: 0 0 10px #00f0ff;
      margin: 10px 0;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 15px;
    }
    .timer svg {
      width: 50px;
      height: 50px;
      fill: none;
      stroke: #00f0ff;
      stroke-width: 2;
      animation: spin 4s linear infinite;
    }
    @keyframes spin {
      100% { transform: rotate(360deg); }
    }
    .progress-bar {
      width: 80%;
      height: 10px;
      background: rgba(255, 255, 255, 0.1);
      border-radius: 5px;
      overflow: hidden;
      margin: 15px auto;
      box-shadow: 0 0 10px #00f0ff;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #00f0ff, #ff00ff);
      width: )rawliteral" + String(progressPercent) + R"rawliteral(%;
      transition: width 1s ease;
    }
    .timeline {
      width: 100%;
      max-height: 400px;
      overflow-y: auto;
      padding: 20px;
      background: rgba(0, 0, 0, 0.5);
      border-radius: 15px;
      box-shadow: 0 0 20px rgba(0, 255, 255, 0.3);
    }
    .task-card {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(0, 255, 255, 0.3);
      border-radius: 10px;
      padding: 15px;
      margin: 10px 0;
      display: flex;
      justify-content: space-between;
      align-items: center;
      transition: transform 0.3s ease, box-shadow 0.3s ease;
      animation: slideIn 0.5s ease forwards;
    }
    .task-card.active {
      background: linear-gradient(45deg, #00f0ff, #ff00ff);
      box-shadow: 0 0 15px #00f0ff;
      transform: scale(1.05);
    }
    .task-card:hover {
      transform: translateY(-5px);
      box-shadow: 0 0 20px rgba(0, 255, 255, 0.5);
    }
    @keyframes slideIn {
      from { transform: translateX(-100%); opacity: 0; }
      to { transform: translateX(0); opacity: 1; }
    }
    .task-info { flex: 1; text-align: left; }
    .task-info h3 { font-size: 1.2rem; margin: 0; color: #e0e0ff; }
    .task-info p { font-size: 0.9rem; margin: 5px 0; color: #b0b0ff; }
    .edit-btn, .plan-select {
      background: linear-gradient(45deg, #00f0ff, #ff00ff);
      border: none;
      padding: 10px 20px;
      border-radius: 10px;
      color: #000;
      font-size: 1rem;
      cursor: pointer;
      box-shadow: 0 0 10px #00f0ff;
      transition: all 0.3s ease;
    }
    .edit-btn:hover, .plan-select:hover {
      transform: scale(1.05);
      box-shadow: 0 0 20px #00f0ff;
    }
    .edit-btn-container, .plan-select-container {
      display: flex;
      justify-content: center;
      margin-top: 20px;
      margin-bottom: 20px;
    }
    .plan-select-container select {
      width: 200px;
      background: rgba(0, 0, 0, 0.7);
      color: #e0e0ff;
    }
    @media (max-width: 600px) {
      .task-name { font-size: 2rem; }
      .timer { font-size: 1.5rem; }
      .timer svg { width: 30px; height: 30px; }
      .task-card { flex-direction: column; align-items: flex-start; }
      .task-info h3 { font-size: 1rem; }
      .task-info p { font-size: 0.8rem; }
      .edit-btn-container, .plan-select-container { margin-top: 15px; margin-bottom: 15px; }
      .plan-select { width: 100%; }
    }
  </style>
</head>
<body>
  <div class='container'>
    <div class='current-task'>
      <div class='task-name'>)rawliteral" + taskName + R"rawliteral(</div>
      <div class='timer'>
        <span>)rawliteral" + String(timeBuf) + R"rawliteral(</span>
        <svg viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg'>
          <path d='M12 2a10 10 0 0 1 10 10 10 10 0 0 1-10 10A10 10 0 0 1 2 12 10 10 0 0 1 12 2m0 2a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 8-8 8 8 0 0 0-8-8z'/>
          <path d='M12 6v6l4 2' stroke-width='2'/>
        </svg>
      </div>
      <div class='progress-bar'>
        <div class='progress-fill'></div>
      </div>
    </div>
    <div class='timeline' id='timeline'>
      )rawliteral";

  for (int i = 0; i < plans[currentPlanIndex].taskCount; i++) {
    html += "<div class='task-card" + String(i == activeIndex ? " active" : "") + "'>";
    html += "<div class='task-info'>";
    html += "<h3>" + String(plans[currentPlanIndex].tasks[i].name) + "</h3>";
    html += "<p>Start: " + formatTime(plans[currentPlanIndex].tasks[i].startHour, plans[currentPlanIndex].tasks[i].startMinute) + "</p>";
    int durH = plans[currentPlanIndex].tasks[i].durationMin / 60;
    int durM = plans[currentPlanIndex].tasks[i].durationMin % 60;
    String durationStr = String(durH) + ":" + (durM < 10 ? "0" : "") + String(durM);
    html += "<p>Duration: " + durationStr + "</p>";
    html += "</div></div>";
  }

  html += R"rawliteral(
    </div>
    <div class='edit-btn-container'>
      <a href='/edit'><button class='edit-btn'>Edit Schedule</button></a>
    </div>
    <div class='plan-select-container'>
      <form method='POST' action='/switchPlan'>
        <select name='planIndex' class='plan-select' onchange='this.form.submit()'>
          )rawliteral";
  
  for (int i = 0; i < planCount; i++) {
    html += "<option value='" + String(i) + "'" + (i == currentPlanIndex ? " selected" : "") + ">" + String(plans[i].name) + "</option>";
  }

  html += R"rawliteral(
        </select>
      </form>
    </div>
  </div>
  <script>
    document.getElementById('timeline').addEventListener('scroll', function() {
      localStorage.setItem('scrollPosition', this.scrollTop);
    });
    window.addEventListener('load', function() {
      const scrollPos = localStorage.getItem('scrollPosition');
      if (scrollPos !== null) {
        document.getElementById('timeline').scrollTop = parseInt(scrollPos);
      }
    });
  </script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleEdit() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Edit Cyberpunk Scheduler</title>
  <link href='https://fonts.googleapis.com/css2?family=Exo+2:wght@300;400;700&display=swap' rel='stylesheet'>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: linear-gradient(135deg, #0d0d2b, #1a1a4e, #3b1a6d, #ff007a);
      background-size: 400% 400%;
      animation: cyberGradient 12s ease infinite;
      color: #e0e0ff;
      font-family: 'Exo 2', sans-serif;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 20px;
    }
    @keyframes cyberGradient {
      0% { background-position: 0% 50%; }
      50% { background-position: 100% 50%; }
      100% { background-position: 0% 50%; }
    }
    .container { max-width: 1000px; width: 100%; }
    h2 {
      font-size: 2.5rem;
      text-shadow: 0 0 10px #00f0ff;
      margin-bottom: 20px;
      animation: neonGlow 1.5s ease-in-out infinite;
    }
    @keyframes neonGlow {
      0%, 100% { text-shadow: 0 0 10px #00f0ff, 0 0 20px #ff00ff; }
      50% { text-shadow: 0 0 20px #00f0ff, 0 0 30px #ff00ff; }
    }
    .task-form {
      width: 100%;
      background: rgba(0, 0, 0, 0.7);
      border-radius: 15px;
      padding: 20px;
      box-shadow: 0 0 20px rgba(0, 255, 255, 0.5);
    }
    .task-row {
      display: flex;
      gap: 10px;
      margin-bottom: 10px;
      align-items: center;
      background: rgba(255, 255, 255, 0.05);
      padding: 10px;
      border-radius: 10px;
      transition: transform 0.3s ease;
    }
    .task-row:hover { transform: translateY(-5px); }
    input[type=text], input[type=number] {
      flex: 1;
      padding: 10px;
      border: none;
      border-radius: 5px;
      background: rgba(255, 255, 255, 0.1);
      color: #e0e0ff;
      font-size: 1rem;
      box-shadow: 0 0 5px #00f0ff;
      transition: box-shadow 0.3s ease;
    }
    input:focus { outline: none; box-shadow: 0 0 10px #00f0ff; }
    .btn-remove {
      background: linear-gradient(45deg, #ff007a, #b0004e);
      border: none;
      padding: 8px 12px;
      border-radius: 5px;
      color: #fff;
      cursor: pointer;
      box-shadow: 0 0 10px #ff007a;
    }
    .btn-remove:hover {
      transform: scale(1.05);
      box-shadow: 0 0 15px #ff007a;
    }
    .btn-add, input[type=submit] {
      background: linear-gradient(45deg, #00f0ff, #ff00ff);
      border: none;
      padding: 10px 20px;
      border-radius: 10px;
      color: #000;
      font-size: 1rem;
      cursor: pointer;
      margin: 10px;
      box-shadow: 0 0 10px #00f0ff;
      transition: all 0.3s ease;
    }
    .btn-add:hover, input[type=submit]:hover {
      transform: scale(1.05);
      box-shadow: 0 0 20px #00f0ff;
    }
    a { color: #00f0ff; text-decoration: none; text-shadow: 0 0 5px #00f0ff; }
    a:hover { text-shadow: 0 0 10px #00f0ff; }
    @media (max-width: 600px) {
      h2 { font-size: 1.8rem; }
      .task-row { flex-direction: column; align-items: stretch; }
      input { font-size: 0.9rem; }
      .btn-remove, .btn-add, input[type=submit] { font-size: 0.9rem; }
    }
  </style>
  <script>
    function addTask() {
      var container = document.getElementById('taskContainer');
      var rowCount = container.childElementCount;
      if (rowCount >= 30) {
        alert('Max 30 tasks allowed.');
        return;
      }
      var row = document.createElement('div');
      row.className = 'task-row';
      row.innerHTML = `
        <input type="text" name="name${rowCount}" maxlength="31" placeholder="Task Name" required>
        <input type="text" name="start${rowCount}" pattern="\\d{2}:\\d{2}" placeholder="HH:MM" required>
        <input type="number" name="duration${rowCount}" min="1" max="1440" placeholder="Duration (min)" required>
        <button type="button" class="btn-remove" onclick="this.parentNode.remove();">Remove</button>
      `;
      container.appendChild(row);
    }
    function addPlan() {
      if (document.getElementById('taskContainer').childElementCount >= 30) {
        alert('Max 5 plans allowed.');
        return;
      }
      var newPlanName = prompt('Enter name for new plan:');
      if (newPlanName && newPlanName.trim() !== '') {
        var form = document.forms['taskForm'];
        form.elements['newPlanName'].value = newPlanName.trim();
        form.submit();
      }
    }
    function confirmDeletePlan() {
      if (confirm('Are you sure you want to delete the plan "' + document.getElementById('planName').innerText + '"?')) {
        window.location.href = '/deletePlan';
      }
    }
  </script>
</head>
<body>
  <div class='container'>
    <h2>Edit Cyberpunk Scheduler - <span id='planName'>)rawliteral" + String(plans[currentPlanIndex].name) + R"rawliteral(</span></h2>
    <form method='POST' action='/save' name='taskForm' class='task-form'>
      <input type='hidden' name='newPlanName' value=''>
      <div id='taskContainer'>
      )rawliteral";

  for (int i = 0; i < plans[currentPlanIndex].taskCount; i++) {
    html += "<div class='task-row'>";
    html += "<input type='text' name='name" + String(i) + "' maxlength='31' value='" + String(plans[currentPlanIndex].tasks[i].name) + "' required>";
    html += "<input type='text' name='start" + String(i) + "' pattern='\\d{2}:\\d{2}' value='" + formatTime(plans[currentPlanIndex].tasks[i].startHour, plans[currentPlanIndex].tasks[i].startMinute) + "' required>";
    html += "<input type='number' name='duration" + String(i) + "' min='1' max='1440' value='" + String(plans[currentPlanIndex].tasks[i].durationMin) + "' required>";
    html += "<button type='button' class='btn-remove' onclick='this.parentNode.remove();'>Remove</button>";
    html += "</div>";
  }

  html += R"rawliteral(
      </div>
      <button type='button' class='btn-add' onclick='addTask()'>Add Task</button>
      <button type='button' class='btn-add' onclick='addPlan()'>Add New Plan</button>
      <button type='button' class='btn-add' onclick='confirmDeletePlan()'>Delete Plan</button>
      <input type='submit' value='Save Schedule'>
    </form>
    <p><a href='/'>Back to Schedule</a></p>
  </div>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  String newPlanName = server.arg("newPlanName");
  if (newPlanName != "" && planCount < MAX_PLANS) {
    strncpy(plans[planCount].name, newPlanName.c_str(), PLAN_NAME_SIZE - 1);
    plans[planCount].name[PLAN_NAME_SIZE - 1] = 0;
    currentPlanIndex = planCount;
    plans[currentPlanIndex].taskCount = 0;
    planCount++;
  }

  int count = 0;
  while (true) {
    if (!server.hasArg("name" + String(count))) break;
    count++;
    if (count >= MAX_TASKS) break;
  }
  plans[currentPlanIndex].taskCount = count;

  for (int i = 0; i < plans[currentPlanIndex].taskCount; i++) {
    String name = server.arg("name" + String(i));
    String time = server.arg("start" + String(i));
    String duration = server.arg("duration" + String(i));

    name.toCharArray(plans[currentPlanIndex].tasks[i].name, sizeof(plans[currentPlanIndex].tasks[i].name));
    plans[currentPlanIndex].tasks[i].name[sizeof(plans[currentPlanIndex].tasks[i].name) - 1] = 0;

    int hour = 0, minute = 0;
    if (time.length() >= 5) {
      hour = time.substring(0, 2).toInt();
      minute = time.substring(3, 5).toInt();
    }
    plans[currentPlanIndex].tasks[i].startHour = hour;
    plans[currentPlanIndex].tasks[i].startMinute = minute;

    plans[currentPlanIndex].tasks[i].durationMin = duration.toInt();
    if (plans[currentPlanIndex].tasks[i].durationMin < 1) plans[currentPlanIndex].tasks[i].durationMin = 1;
  }

  savePlansToEEPROM();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSwitchPlan() {
  if (server.hasArg("planIndex")) {
    int newIndex = server.arg("planIndex").toInt();
    if (newIndex >= 0 && newIndex < planCount) {
      currentPlanIndex = newIndex;
      savePlansToEEPROM();
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDeletePlan() {
  if (planCount > 1) { // حداقل یک برنامه باید باقی بماند
    // جابجایی برنامه‌های بعدی برای پر کردن جای برنامه حذف‌شده
    for (int i = currentPlanIndex; i < planCount - 1; i++) {
      plans[i] = plans[i + 1];
    }
    planCount--;
    if (currentPlanIndex >= planCount) {
      currentPlanIndex = planCount - 1; // اگر برنامه فعلی آخرین برنامه بود، به برنامه قبلی برو
    }
    savePlansToEEPROM(); // ذخیره تغییرات در EEPROM
  }
  server.sendHeader("Location", "/");
  server.send(303);
}
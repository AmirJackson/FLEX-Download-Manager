const PORTS = [48765, 48766, 48767, 48768, 48769];

const statusDot = document.getElementById("status-dot");
const statusText = document.getElementById("status-text");
const statusHint = document.getElementById("status-hint");
const checkAgain = document.getElementById("check-again");
const autoCapture = document.getElementById("auto-capture");
const mediaPanel = document.getElementById("media-panel");
const version = document.getElementById("version");
const openDownloadSettings = document.getElementById("open-download-settings");
const savePromptTipText = document.getElementById("save-prompt-tip-text");

function message(key) {
  return chrome.i18n.getMessage(key) || "";
}

function localize() {
  const language = (chrome.i18n.getUILanguage() || "").toLowerCase();
  if (language.startsWith("ar")) {
    document.documentElement.dir = "rtl";
    document.documentElement.lang = "ar";
  }

  document.querySelectorAll("[data-i18n]").forEach((element) => {
    const text = message(element.getAttribute("data-i18n"));
    if (text) element.textContent = text;
  });

  const manifest = chrome.runtime.getManifest();
  version.textContent = `${message("version")} ${manifest.version}`;
}

async function pingPort(port) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 350);
  try {
    const response = await fetch(`http://127.0.0.1:${port}/ping`, {
      cache: "no-store",
      signal: controller.signal
    });
    if (!response.ok) return false;
    const data = await response.json();
    return data && data.ok === true;
  } catch (error) {
    return false;
  } finally {
    clearTimeout(timer);
  }
}

async function refreshStatus() {
  statusDot.className = "dot checking";
  statusText.textContent = message("popupChecking");
  statusHint.textContent = "";

  let connected = false;
  for (const port of PORTS) {
    if (await pingPort(port)) {
      connected = true;
      break;
    }
  }

  if (connected) {
    statusDot.className = "dot online";
    statusText.textContent = message("popupConnected");
    statusHint.textContent = message("popupHintOnline");
  } else {
    statusDot.className = "dot offline";
    statusText.textContent = message("popupOffline");
    statusHint.textContent = message("popupHintOffline");
  }
}

async function loadSettings() {
  const result = await chrome.storage.local.get({
    autoCapture: true,
    mediaPanel: true
  });
  autoCapture.checked = result.autoCapture !== false;
  mediaPanel.checked = result.mediaPanel !== false;
}

autoCapture.addEventListener("change", async () => {
  await chrome.storage.local.set({ autoCapture: autoCapture.checked });
  if (autoCapture.checked) refreshStatus();
});

mediaPanel.addEventListener("change", async () => {
  await chrome.storage.local.set({ mediaPanel: mediaPanel.checked });
});

checkAgain.addEventListener("click", refreshStatus);

openDownloadSettings.addEventListener("click", async () => {
  try {
    await chrome.tabs.create({ url: "chrome://settings/downloads" });
  } catch (error) {
    savePromptTipText.textContent = message("settingsOpenFailed");
  }
});

localize();
loadSettings();
refreshStatus();

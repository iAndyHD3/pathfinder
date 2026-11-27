// ==UserScript==
// @name         GDBrowser .gdlvl Downloader
// @namespace    rrr.pathfinder.vm-gdbrowser-gdlvl
// @version      1.2.1
// @description  Adds a "Download .gdlvl" button to GDBrowser level pages. The
//               downloadGJLevel22 request is made from the browser (the GD
//               servers allow cross-origin database requests) and the response
//               is packaged into a .gdlvl file that pathfinder-cli can parse.
// @author       RRR pathfinder
// @match        https://gdbrowser.com/*
// @run-at       document-end
// @grant        GM_xmlhttpRequest
// @connect      www.boomlings.com
// @noframes
// @license      MIT
// ==/UserScript==

(function () {
  "use strict";

  // Endpoint parameters from the GD 2.2 client.
  const DOWNLOAD_URL = "https://www.boomlings.com/database/downloadGJLevel22.php";
  const SECRET = "Wmfd2893gb7";
  const GAME_VERSION = "22";
  const BINARY_VERSION = "51";
  const REQUEST_TIMEOUT_MS = 20000;

  // Matches https://gdbrowser.com/<id> (level pages only).
  const LEVEL_ID_PATTERN = /^\/(\d+)\/?$/;

  const kCEK_VALUE = "4"; // kCEK key of the generated plist (4 = 2.2 local level).

  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  // Escapes text for use inside <s>...</s> plist values.
  function xmlEscape(text) {
    return String(text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&apos;");
  }

  // Encodes bytes as the URL-safe base64 GD uses (no '=' padding).
  function bytesToBase64Url(bytes) {
    let binary = "";
    const chunkSize = 0x8000;
    for (let i = 0; i < bytes.length; i += chunkSize) {
      binary += String.fromCharCode.apply(
        null, bytes.subarray(i, i + chunkSize));
    }
    return btoa(binary)
      .replace(/\+/g, "-")
      .replace(/\//g, "_")
      .replace(/=+$/, "");
  }

  // Gzips bytes with the browser's CompressionStream.
  async function gzipBytes(bytes) {
    if (typeof CompressionStream === "undefined") {
      throw new Error("This browser does not support CompressionStream");
    }
    const stream = new Blob([bytes]).stream()
      .pipeThrough(new CompressionStream("gzip"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  // Level strings and .gdlvl plists are stored as URL-safe base64 of gzip data.
  async function encodeGdString(text) {
    return bytesToBase64Url(await gzipBytes(new TextEncoder().encode(text)));
  }  // ---------------------------------------------------------------------------
  // Response parsing
  // ---------------------------------------------------------------------------

  // Parses a 'key:value:key:value' server response. Values are assumed not to
  // contain ':' (true for level IDs, names as stored, and base64 payloads).
  function parseKeyValueObject(payload) {
    const tokens = payload.split(":");
    const object = {};
    for (let i = 0; i + 1 < tokens.length; i += 2) {
      object[tokens[i]] = tokens[i + 1];
    }
    return object;
  }

  // Returns a filesystem-safe file name (no path separators or reserved chars).
  function sanitizeFileName(name, fallback) {
    const cleaned = name.replace(/[\\/:*?"<>|]/g, "-")
      .replace(/[\u0000-\u001f]/g, " ")
      .trim()
      .replace(/\.+$/, "");
    return cleaned === "" ? fallback : cleaned;
  }

  // ---------------------------------------------------------------------------
  // .gdlvl assembly
  //
  // A .gdlvl file is a text file holding base64url(gzip(<plist XML>)), where
  // the plist stores the level metadata and a 'k4' string with the level
  // string. This matches the layout pathfinder-cli parses (see cli/gdlvl.cpp
  // and the repo README).
  //
  // The downloadGJLevel22 response already holds the level string (key "4")
  // in GDBrowser's export form: base64url(gzip(<level string>)). It is stored
  // in the plist as-is, exactly like the .gdlvl files in testlevles/.
  //
  // The remaining keys mirror what the game itself writes when saving a level
  // (see testlevles/comformance/*.gdlvl). The game's importer expects this
  // key set, so a bare kCEK/k2/k4 plist is rejected on import.
  // ---------------------------------------------------------------------------

  // k156 = Editor Build Stats: 23 comma values — format version "1", twenty
  // zeros, the copied-from level ID, and the copied-from level version
  // (https://boomlings.dev/resources/client/level-components/build-stats-string).
  function buildK156(levelId, levelVersion) {
    const values = ["1"];
    for (let i = 0; i < 20; i++) values.push("0");
    values.push(levelId, levelVersion || "0");
    return values.join(",");
  }

  async function buildGdlvl(levelName, encodedLevelString, level) {
    // Server key 12 is the official song ID (0 = custom song); the game only
    // writes k8 for official songs and k45 for custom ones.
    const officialSong = level["12"] || "0";
    const customSong = level["35"] || "0";
    // Server keys 37/5 are the total coins and the level version.
    const totalCoins = level["37"] || "0";
    let plist =
      '<?xml version="1.0"?>' +
      '<plist version="1.0" gjver="2.0">' +
      "<dict>" +
      "<k>kCEK</k><i>" + kCEK_VALUE + "</i>" +
      "<k>k23</k><i>3</i>" +
      "<k>k2</k><s>" + xmlEscape(levelName) + "</s>" +
      "<k>k4</k><s>" + encodedLevelString + "</s>";
    if (officialSong !== "0") {
      plist += "<k>k8</k><i>" + officialSong + "</i>";
    }
    plist +=
      "<k>k13</k><t />" +
      "<k>k21</k><i>2</i>" +
      "<k>k156</k><s>" + xmlEscape(buildK156(level["1"], level["5"])) + "</s>";
    // The game omits zero-valued optional keys when saving (compare the
    // ReTraY and The Nightmare exports), so k64/k45 are only written when
    // nonzero. k81 (edit seconds on copies) is always 0 here and omitted.
    if (totalCoins !== "0") {
      plist += "<k>k64</k><i>" + totalCoins + "</i>";
    }
    if (customSong !== "0") {
      plist += "<k>k45</k><i>" + customSong + "</i>";
    }
    plist +=
      "<k>k50</k><i>" + BINARY_VERSION + "</i>" +
      "<k>kI1</k><r>0</r>" +
      "<k>kI2</k><r>0</r>" +
      "<k>kI3</k><r>0</r>" +
      "<k>kI6</k><d />" +
      "</dict>" +
      "</plist>";
    return encodeGdString(plist);
  }

  // ---------------------------------------------------------------------------
  // Server request
  // ---------------------------------------------------------------------------

  // GM_xmlhttpRequest wrapped in a promise. This bypasses the page's CORS
  function gmFetch(url, options) {
    return new Promise((resolve, reject) => {
      GM_xmlhttpRequest({
        method: options.method || "GET",
        url,
        headers: options.headers,
        data: options.body,
        timeout: REQUEST_TIMEOUT_MS,
        onload: (response) => resolve(response),
        onerror: () => reject(new Error(
          "network error (request blocked or failed)")),
        ontimeout: () => reject(new Error(
          "request timed out after " + (REQUEST_TIMEOUT_MS / 1000) + "s")),
      });
    });
  }

  // Downloads the level object from the GD servers. Uses GM_xmlhttpRequest
  // because the GD servers send no CORS headers, so the page's fetch() is
  // blocked when called from gdbrowser.com. The GD servers reject requests
  // with any non-empty User-Agent (HTTP 403), so the browser's UA header is
  // explicitly blanked for this request.
  async function downloadLevel(levelId) {
    const response = await gmFetch(DOWNLOAD_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        "User-Agent": "",
      },
      body: new URLSearchParams({
        levelID: levelId,
        secret: SECRET,
        gameVersion: GAME_VERSION,
        binaryVersion: BINARY_VERSION,
      }).toString(),
    });
    const text = response.responseText.trim();
    if (response.status < 200 || response.status >= 300) {
      throw new Error("server returned HTTP " + response.status);
    }
    if (text === "" || text === "-1") {
      throw new Error("level not found on the GD servers");
    }
    return parseKeyValueObject(text.split("#")[0]);
  }

  // ---------------------------------------------------------------------------
  // UI
  // ---------------------------------------------------------------------------

  function showToast(message, isError) {
    let host = document.getElementById("vm-gdlvl-toast");
    if (!host) {
      host = document.createElement("div");
      host.id = "vm-gdlvl-toast";
      host.style.cssText = [
        "position: fixed",
        "left: 50%",
        "bottom: 4vh",
        "transform: translateX(-50%)",
        "background: #7a4a1f",
        "border: 2px solid #a05a20",
        "border-radius: 8px",
        "padding: 10px 18px",
        "color: #fff",
        "font: bold 16px/1.3 Pusab, sans-serif",
        "text-shadow: 0 0 3px rgba(0, 0, 0, .8)",
        "box-shadow: 0 4px 14px rgba(0, 0, 0, .5)",
        "z-index: 99999",
        "transition: opacity .25s",
        "opacity: 0",
        "pointer-events: none",
      ].join(";");
      document.body.appendChild(host);
    }
    host.textContent = message;
    host.style.background = isError ? "#8a1f1f" : "#7a4a1f";
    host.style.borderColor = isError ? "#c23a3a" : "#a05a20";
    host.style.opacity = "1";
    clearTimeout(showToast.timer);
    showToast.timer = setTimeout(() => { host.style.opacity = "0"; }, 2600);
  }

  function triggerDownload(fileName, fileText) {
    const url = URL.createObjectURL(
      new Blob([fileText], { type: "application/octet-stream" }));
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = fileName;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  }

  // Handles a click on the injected button.
  async function handleDownload(levelId, button) {
    const busy = button.querySelector(".vm-gdlvl-btn-img");
    button.style.pointerEvents = "none";
    if (busy) busy.style.opacity = "0.5";
    try {
      const level = await downloadLevel(levelId);
      // Key "4" is the level string already encoded as base64url(gzip(...)).
      const encodedLevelString = level["4"];
      if (!encodedLevelString) {
        throw new Error("response contains no level string");
      }
      const levelName = sanitizeFileName(
        level["2"] || "", "level-" + levelId);
      const fileText = await buildGdlvl(levelName, encodedLevelString, level);
      triggerDownload(levelName + ".gdlvl", fileText);
      showToast("Saved " + levelName + ".gdlvl");
    } catch (error) {
      const reason = (error && error.message) || "unknown error";
      showToast("Download failed: " + reason, true);
      console.error("[vm-gdbrowser-gdlvl] download failed:", error);
    } finally {
      button.style.pointerEvents = "";
      if (busy) busy.style.opacity = "";
    }
  }

  // Adds the download button to the level page's side button column.
  function injectButton(levelId) {
    const container = document.querySelector(".levelButtons");
    if (!container) {
      return false;
    }
    const button = document.createElement("a");
    button.className = "centerflex";
    button.title = "Download .gdlvl";
    button.style.cursor = "pointer";

    const image = document.createElement("img");
    image.className = "gdButton sideButton vm-gdlvl-btn-img";
    image.src = "/assets/download.png";
    image.alt = "Download .gdlvl";
    button.appendChild(image);

    button.addEventListener("click", (event) => {
      event.preventDefault();
      handleDownload(levelId, button);
    });
    container.appendChild(button);
    return true;
  }

  // ---------------------------------------------------------------------------
  // Entry point
  // ---------------------------------------------------------------------------

  function main() {
    // Not a level page (search, /u/<user>, /comments/... etc.).
    const match = location.pathname.match(LEVEL_ID_PATTERN);
    if (!match) {
      return;
    }
    const levelId = match[1];

    // The button column is rendered with the page, but retry briefly in case
    // this script runs before it exists.
    let attempts = 0;
    const tryInject = () => {
      if (injectButton(levelId) || ++attempts >= 50) {
        return;
      }
      setTimeout(tryInject, 100);
    };
    tryInject();
  }

  main();
})();

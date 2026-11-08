// PS5Term website: the language, the light/dark switch, copy buttons and the newest release on
// the download button. No build step and no dependencies. SPDX-License-Identifier: GPL-3.0-or-later
//
// The words come in the visitor's language: English is in index.html, the others in
// i18n/<code>.json (scripts/site-i18n.mjs checks them against the page). The language itself is
// picked in index.html's <head>, before anything is drawn, the same way as PS5Upload's site.
"use strict";

const REPO = "phantomptr/ps5term";
const root = document.documentElement;
const store = {
  get: (k) => {
    try {
      return localStorage.getItem(k);
    } catch {
      return null;
    }
  },
  set: (k, v) => {
    try {
      localStorage.setItem(k, v);
    } catch {
      /* private window: the choice lasts this visit */
    }
  },
};

// ── Language ───────────────────────────────────────────────────────────────

/** The same languages, in the same order, as PS5Upload and phantomptr.com. */
const LANGS = [
  ["en", "English"],
  ["zh-CN", "简体中文"],
  ["zh-TW", "繁體中文"],
  ["hi", "हिन्दी"],
  ["es", "Español"],
  ["ar", "العربية"],
  ["bn", "বাংলা"],
  ["pl", "Polski"],
  ["pt-BR", "Português (Brasil)"],
  ["ru", "Русский"],
  ["ja", "日本語"],
  ["de", "Deutsch"],
  ["fr", "Français"],
  ["ko", "한국어"],
  ["tr", "Türkçe"],
  ["vi", "Tiếng Việt"],
  ["id", "Bahasa Indonesia"],
  ["it", "Italiano"],
  ["th", "ไทย"],
  ["hu", "Magyar"],
  ["fa", "فارسی"],
];
const lang = LANGS.some(([c]) => c === root.lang) ? root.lang : "en";
let words = {};

/** A string in the visitor's language, `en` when it has no translation; {name} takes vars.name. */
function t(key, en, vars = {}) {
  let s = words[key] ?? en;
  for (const [k, v] of Object.entries(vars)) s = s.replaceAll(`{${k}}`, v);
  return s;
}

async function loadWords() {
  if (lang === "en") return;
  try {
    const res = await fetch(`i18n/${lang}.json`);
    if (res.ok) words = await res.json();
  } catch {
    /* stays English */
  }
}

/** data-i18n is an element's content (which may hold <b> and <a>), data-i18n-attr is
 *  "attribute:key" pairs split by ";". */
function translatePage() {
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const s = words[el.dataset.i18n];
    if (s !== undefined) el.innerHTML = s;
  });
  document.querySelectorAll("[data-i18n-attr]").forEach((el) => {
    for (const pair of el.dataset.i18nAttr.split(";")) {
      const [attr, key] = pair.split(":");
      if (words[key] !== undefined) el.setAttribute(attr, words[key]);
    }
  });
}

const select = document.getElementById("lang");
select.innerHTML = LANGS.map(([c, label]) => `<option value="${c}" lang="${c}">${label}</option>`).join("");
select.value = lang;
select.addEventListener("change", () => {
  store.set("ps5term.site.lang", select.value);
  const url = new URL(location.href);
  url.searchParams.set("lang", select.value);
  location.href = url.href;
});

// ── Theme ──────────────────────────────────────────────────────────────────

const systemDark = () => window.matchMedia("(prefers-color-scheme: dark)").matches;
function applyTheme(theme) {
  if (theme) root.dataset.theme = theme;
  else delete root.dataset.theme;
}
applyTheme(store.get("ps5term.site.theme"));
document.getElementById("theme").addEventListener("click", () => {
  const dark = root.dataset.theme ? root.dataset.theme === "dark" : systemDark();
  const next = dark ? "light" : "dark";
  store.set("ps5term.site.theme", next);
  applyTheme(next);
});

// ── Copy buttons ───────────────────────────────────────────────────────────

document.addEventListener("click", async (e) => {
  const btn = e.target.closest("[data-copy]");
  if (!btn) return;
  const text = document.getElementById(btn.dataset.copy)?.textContent ?? "";
  try {
    await navigator.clipboard.writeText(text);
    const was = btn.textContent;
    btn.textContent = t("ssh.copied", "Copied");
    setTimeout(() => (btn.textContent = was), 1500);
  } catch {
    /* no clipboard access: the command stays selectable */
  }
});

// ── The newest release ─────────────────────────────────────────────────────

async function showRelease() {
  try {
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) return;
    const release = await res.json();
    const zip = (release.assets || []).find((a) => a.name.endsWith(".zip"));
    if (zip)
      for (const id of ["hero-download", "guide-download"]) {
        const link = document.getElementById(id);
        if (link) link.href = zip.browser_download_url;
      }
    document.getElementById("hero-version").textContent = t("hero.version", "Version {tag} · free and open source", {
      tag: release.tag_name,
    });
  } catch {
    /* the button still opens the releases page */
  }
}

loadWords().then(() => {
  translatePage();
  root.classList.remove("i18n-wait");
  showRelease();
});

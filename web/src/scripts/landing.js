const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

/* ------------------------------------------------------------------ menu -- */

const menuToggle = document.getElementById("menu-toggle");
const siteMenu = document.getElementById("site-menu");
const pageMain = document.getElementById("main");
const pageFooter = document.querySelector(".site-foot");
const desktopQuery = window.matchMedia("(min-width: 901px)");

function menuIsOpen() {
  return menuToggle?.getAttribute("aria-expanded") === "true";
}

function setMenu(open) {
  if (!menuToggle || !siteMenu) return;

  menuToggle.setAttribute("aria-expanded", String(open));
  menuToggle.setAttribute("aria-label", open ? "Close menu" : "Open menu");
  siteMenu.setAttribute("aria-hidden", String(!open));
  siteMenu.classList.toggle("is-open", open);
  document.body.classList.toggle("menu-open", open);

  if (pageMain) pageMain.inert = open;
  if (pageFooter) pageFooter.inert = open;

  if (open) {
    const firstLink = siteMenu.querySelector("a");
    window.setTimeout(() => firstLink?.focus(), 350);
  }
}

if (menuToggle && siteMenu) {
  menuToggle.addEventListener("click", () => {
    setMenu(!menuIsOpen());
  });

  siteMenu.addEventListener("click", (event) => {
    if (event.target instanceof Element && event.target.closest("a")) {
      setMenu(false);
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && menuIsOpen()) {
      setMenu(false);
      menuToggle.focus();
    }
  });

  desktopQuery.addEventListener("change", (event) => {
    if (event.matches) setMenu(false);
  });
}

/* ---------------------------------------------------------------- reveal -- */

const revealElements = document.querySelectorAll("[data-reveal]");

if (reduceMotion || !("IntersectionObserver" in window) || revealElements.length === 0) {
  revealElements.forEach((element) => element.classList.add("is-visible"));
} else {
  const revealObserver = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("is-visible");
        revealObserver.unobserve(entry.target);
      });
    },
    { threshold: 0.1, rootMargin: "0px 0px -10% 0px" },
  );

  revealElements.forEach((element) => revealObserver.observe(element));
}

/* --------------------------------------------------------------- tagline -- */

const taglines = document.querySelectorAll("[data-tagline]");

taglines.forEach((tagline) => {
  const words = tagline.querySelectorAll(".tagline-word");

  if (reduceMotion || !("IntersectionObserver" in window)) {
    words.forEach((word) => word.classList.add("is-lit"));
    return;
  }

  const wordObserver = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("is-lit");
        wordObserver.unobserve(entry.target);
      });
    },
    { threshold: 0.1, rootMargin: "0px 0px -20% 0px" },
  );

  words.forEach((word, index) => {
    word.style.transitionDelay = `${index * 45}ms`;
    wordObserver.observe(word);
  });
});

/* ------------------------------------------------------------------ copy -- */

async function writeClipboard(text) {
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(text);
    return;
  }

  const field = document.createElement("textarea");
  field.value = text;
  field.setAttribute("readonly", "");
  field.style.position = "fixed";
  field.style.top = "-1000px";
  document.body.appendChild(field);
  field.select();

  const copied = document.execCommand("copy");
  field.remove();

  if (!copied) {
    throw new Error("Copy command was rejected.");
  }
}

document.querySelectorAll("[data-copy]").forEach((button) => {
  button.addEventListener("click", async () => {
    const targetId = button.getAttribute("data-copy");
    const target = targetId ? document.getElementById(targetId) : null;
    if (!target) return;

    const label = button.querySelector(".copy-label");
    const original = button.getAttribute("data-label") || "Copy";
    const done = button.getAttribute("data-done") || "Copied";
    const failed = button.getAttribute("data-failed") || "Copy failed";

    button.classList.remove("is-copied", "is-failed");

    try {
      await writeClipboard(target.innerText);
      button.classList.add("is-copied");
      if (label) label.textContent = done;
    } catch {
      button.classList.add("is-failed");
      if (label) label.textContent = failed;
    }

    window.setTimeout(() => {
      button.classList.remove("is-copied", "is-failed");
      if (label) label.textContent = original;
    }, 1800);
  });
});

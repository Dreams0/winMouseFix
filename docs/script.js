(function () {
  'use strict';

  var root = document.documentElement;
  var reduceQuery = window.matchMedia('(prefers-reduced-motion: reduce)');
  var isReduced = reduceQuery.matches;

  /* ---------- header + scroll progress ---------- */
  var header = document.getElementById('siteHeader');
  var progress = document.getElementById('scrollProgress');
  var ticking = false;

  function updateScroll() {
    var y = window.scrollY || window.pageYOffset || 0;
    if (header) header.classList.toggle('scrolled', y > 12);
    if (progress) {
      var max = Math.max(1, root.scrollHeight - window.innerHeight);
      progress.style.transform = 'scaleX(' + Math.min(1, y / max).toFixed(4) + ')';
    }
    ticking = false;
  }
  window.addEventListener('scroll', function () {
    if (!ticking) { ticking = true; window.requestAnimationFrame(updateScroll); }
  }, { passive: true });
  updateScroll();

  /* ---------- menu ---------- */
  var menuButton = document.getElementById('menuToggle');
  var nav = document.getElementById('primaryNav');

  function closeMenu() {
    if (!nav || !menuButton) return;
    nav.classList.remove('open');
    menuButton.setAttribute('aria-expanded', 'false');
    menuButton.setAttribute('aria-label', '打开菜单');
  }

  function toggleMenu() {
    if (!nav || !menuButton) return;
    var open = nav.classList.toggle('open');
    menuButton.setAttribute('aria-expanded', open ? 'true' : 'false');
    menuButton.setAttribute('aria-label', open ? '关闭菜单' : '打开菜单');
  }

  if (menuButton) menuButton.addEventListener('click', function (event) {
    event.stopPropagation();
    toggleMenu();
  });
  if (nav) {
    nav.addEventListener('click', function (event) {
      var link = event.target.closest('a');
      if (link) closeMenu();
    });
  }
  document.addEventListener('click', function (event) {
    if (nav && nav.classList.contains('open') &&
        !nav.contains(event.target) && !menuButton.contains(event.target)) {
      closeMenu();
    }
  });
  document.addEventListener('keydown', function (event) {
    if (event.key === 'Escape') closeMenu();
  });
  window.addEventListener('resize', function () {
    if (window.innerWidth > 920) closeMenu();
  }, { passive: true });

  /* ---------- reveal on scroll ---------- */
  var reveals = Array.prototype.slice.call(document.querySelectorAll('.reveal'));
  var hasIO = 'IntersectionObserver' in window;

  if (isReduced || !hasIO) {
    reveals.forEach(function (el) { el.classList.add('visible'); });
  } else {
    var revealObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          entry.target.classList.add('visible');
          revealObserver.unobserve(entry.target);
        }
      });
    }, { threshold: 0.14, rootMargin: '0px 0px -6% 0px' });
    reveals.forEach(function (el) { revealObserver.observe(el); });
  }

  /* ---------- active navigation ---------- */
  var navLinks = Array.prototype.slice.call(document.querySelectorAll('.nav-links a'));
  var targets = navLinks.map(function (a) {
    var hash = a.getAttribute('href') || '';
    return hash.charAt(0) === '#' ? document.getElementById(hash.slice(1)) : null;
  }).filter(Boolean);

  function setActive(id) {
    navLinks.forEach(function (a) {
      a.classList.toggle('active', a.getAttribute('href') === '#' + id);
    });
  }

  if (hasIO && targets.length) {
    var navObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) setActive(entry.target.id);
      });
    }, { rootMargin: '-38% 0px -54% 0px', threshold: 0 });
    targets.forEach(function (s) { navObserver.observe(s); });
  }

  navLinks.forEach(function (a) {
    a.addEventListener('click', function () {
      var hash = a.getAttribute('href') || '';
      if (hash.charAt(0) === '#') setActive(hash.slice(1));
    });
  });

  /* ---------- number counters ---------- */
  var counters = Array.prototype.slice.call(document.querySelectorAll('.stat-num'));

  function animateCounter(el) {
    var target = parseFloat(el.getAttribute('data-count')) || 0;
    var prefix = el.getAttribute('data-prefix') || '';
    var suffix = el.getAttribute('data-suffix') || '';
    if (isReduced) {
      el.textContent = prefix + target + suffix;
      return;
    }
    var start = performance.now();
    var duration = 1250;
    function frame(now) {
      var p = Math.min(1, (now - start) / duration);
      var eased = 1 - Math.pow(1 - p, 3);
      el.textContent = prefix + Math.round(target * eased) + suffix;
      if (p < 1) requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  }

  if (counters.length) {
    if (isReduced || !hasIO) {
      counters.forEach(animateCounter);
    } else {
      var counterObserver = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          if (entry.isIntersecting) {
            animateCounter(entry.target);
            counterObserver.unobserve(entry.target);
          }
        });
      }, { threshold: 0.6 });
      counters.forEach(function (el) { counterObserver.observe(el); });
    }
  }

  /* ---------- pointer tilt ---------- */
  var finePointer = window.matchMedia('(pointer: fine)').matches;
  if (!isReduced && finePointer) {
    Array.prototype.forEach.call(document.querySelectorAll('[data-tilt]'), function (el) {
      var rect = null;
      var strength = parseFloat(el.getAttribute('data-tilt')) || 6;
      el.addEventListener('pointerenter', function () {
        rect = el.getBoundingClientRect();
        el.classList.add('is-tilting');
      });
      el.addEventListener('pointermove', function (event) {
        if (!rect) rect = el.getBoundingClientRect();
        if (!rect.width || !rect.height) return;
        var x = (event.clientX - rect.left) / rect.width - 0.5;
        var y = (event.clientY - rect.top) / rect.height - 0.5;
        el.style.transform = 'perspective(1100px) rotateX(' + (-y * strength).toFixed(2) +
          'deg) rotateY(' + (x * strength).toFixed(2) + 'deg)';
      });
      el.addEventListener('pointerleave', function () {
        el.classList.remove('is-tilting');
        el.style.transform = '';
        rect = null;
      });
    });
  }

  /* ---------- smooth scroll demo ---------- */
  var viewport = document.getElementById('demoViewport');
  var content = document.getElementById('demoContent');
  var thumb = document.getElementById('demoThumb');
  var speedBar = document.getElementById('speedBar');
  var inertiaBar = document.getElementById('inertiaBar');
  var speedValue = document.getElementById('speedValue');
  var inertiaValue = document.getElementById('inertiaValue');
  var demoTip = document.getElementById('demoHint');
  var slowButton = document.getElementById('demoSlow');
  var fastButton = document.getElementById('demoFast');
  var resetButton = document.getElementById('demoReset');

  if (viewport && content) {
    var pos = 0, target = 0, inertiaVel = 0;
    var lastWheelAt = 0, lastEventAt = 0;
    var speedShown = 0, inertiaShown = 0;
    var rafId = 0, lastFrameAt = 0;
    var autoTimer = 0, autoIndex = 0, autoTotal = 0;

    var clamp = function (value, min, max) { return Math.min(max, Math.max(min, value)); };
    var maxScroll = function () { return Math.max(0, content.scrollHeight - viewport.clientHeight); };

    function updateThumb() {
      if (!thumb) return;
      var track = thumb.parentElement;
      if (!track) return;
      var travel = Math.max(0, track.clientHeight - thumb.offsetHeight);
      var max = maxScroll();
      var ratio = max > 0 ? clamp(pos / max, 0, 1) : 0;
      thumb.style.transform = 'translateY(' + (ratio * travel).toFixed(1) + 'px)';
    }

    function renderHud() {
      if (speedBar) speedBar.style.width = (clamp(speedShown, 0, 1) * 100).toFixed(0) + '%';
      if (inertiaBar) inertiaBar.style.width = (clamp(inertiaShown, 0, 1) * 100).toFixed(0) + '%';
      if (speedValue) speedValue.textContent = speedShown.toFixed(2);
      if (inertiaValue) inertiaValue.textContent = inertiaShown > 0.16 ? '滑行' : '精准';
    }

    function ensureFrame() {
      if (!rafId) {
        lastFrameAt = performance.now();
        rafId = requestAnimationFrame(step);
      }
    }

    function stopAuto() {
      if (autoTimer) { clearInterval(autoTimer); autoTimer = 0; }
    }

    function impulse(delta, speed) {
      var max = maxScroll();
      target = clamp(target + delta * (1 + speed * 1.4), 0, max);
      inertiaVel = clamp(inertiaVel + delta * speed * 4.2, -1400, 1400);
      speedShown = Math.max(speedShown, clamp(speed, 0, 1));
      inertiaShown = Math.max(inertiaShown, clamp(speed * 1.4, 0, 1));
      lastWheelAt = performance.now();
      if (demoTip) demoTip.classList.add('interacted');
      renderHud();
      ensureFrame();
    }

    function step(now) {
      var dt = (now - lastFrameAt) / 1000;
      if (!(dt > 0)) dt = 0.016;
      dt = Math.min(0.04, dt);
      lastFrameAt = now;

      if (performance.now() - lastWheelAt > 85 && Math.abs(inertiaVel) > 1) {
        target += inertiaVel * dt;
        inertiaVel *= Math.exp(-dt / 0.3);
        if (Math.abs(inertiaVel) < 8) inertiaVel = 0;
        target = clamp(target, 0, maxScroll());
      }

      pos += (target - pos) * (1 - Math.exp(-dt / 0.055));
      if (Math.abs(target - pos) < 0.2) pos = target;
      content.style.transform = 'translate3d(0,' + (-pos).toFixed(2) + 'px,0)';

      speedShown *= Math.exp(-dt / 0.22);
      inertiaShown *= Math.exp(-dt / 0.36);
      if (speedShown < 0.01) speedShown = 0;
      if (inertiaShown < 0.01) inertiaShown = 0;
      renderHud();
      updateThumb();

      if (Math.abs(target - pos) < 0.2 && Math.abs(inertiaVel) < 0.5 && !autoTimer) {
        pos = target;
        rafId = 0;
        return;
      }
      rafId = requestAnimationFrame(step);
    }

    viewport.addEventListener('wheel', function (event) {
      event.preventDefault();
      stopAuto();
      var now = performance.now();
      var dt = Math.max(10, Math.min(120, now - (lastEventAt || now - 60)));
      lastEventAt = now;
      var delta = event.deltaY;
      if (event.deltaMode === 1) delta *= 16;
      else if (event.deltaMode === 2) delta *= viewport.clientHeight;
      var speed = clamp((Math.abs(delta) / dt - 0.8) / 4.8, 0, 1);
      impulse(delta, speed);
    }, { passive: false });
    // touch / pointer drag mirrors the wheel demo for devices without a wheel
    var dragging = false, dragY = 0, dragT = 0, dragVel = 0;

    function endDrag() {
      if (!dragging) return;
      dragging = false;
      viewport.classList.remove('is-dragging');
      inertiaVel = clamp(-dragVel * 350, -1800, 1800);
      lastWheelAt = performance.now();
      if (demoTip) demoTip.classList.add('interacted');
      ensureFrame();
    }

    viewport.addEventListener('pointerdown', function (event) {
      if (event.pointerType === 'mouse' && event.button !== 0) return;
      dragging = true;
      dragY = event.clientY;
      dragT = performance.now();
      dragVel = 0;
      stopAuto();
      viewport.classList.add('is-dragging');
      if (viewport.setPointerCapture) {
        try { viewport.setPointerCapture(event.pointerId); } catch (error) {}
      }
    });

    viewport.addEventListener('pointermove', function (event) {
      if (!dragging) return;
      var now = performance.now();
      var dt = Math.max(8, now - dragT);
      var dy = event.clientY - dragY;
      dragY = event.clientY;
      dragT = now;
      dragVel = -dy / dt;
      target = clamp(target - dy, 0, maxScroll());
      var speed = clamp(Math.abs(dy) / dt / 0.9, 0, 1);
      speedShown = Math.max(speedShown, speed);
      inertiaShown = Math.max(inertiaShown, clamp(speed * 1.4, 0, 1));
      lastWheelAt = now;
      if (demoTip) demoTip.classList.add('interacted');
      renderHud();
      ensureFrame();
    });

    viewport.addEventListener('pointerup', endDrag);
    viewport.addEventListener('pointercancel', endDrag);
    viewport.addEventListener('lostpointercapture', function () {
      if (dragging) endDrag();
    });

    if (window.matchMedia('(pointer: coarse)').matches && demoTip) {
      demoTip.innerHTML = '<span class="demo-mouse"><i></i></span>上下拖动，试试手感';
    }


    function startAuto(kind) {
      stopAuto();
      autoIndex = 0;
      autoTotal = kind === 'slow' ? 6 : 12;
      var interval = kind === 'slow' ? 230 : 28;
      autoTimer = setInterval(function () {
        if (autoIndex >= autoTotal) { stopAuto(); return; }
        var speed = kind === 'slow' ? 0.03 : clamp(0.35 + autoIndex * 0.055, 0, 1);
        impulse(kind === 'slow' ? 72 : 118, speed);
        autoIndex++;
      }, interval);
      if (demoTip) demoTip.classList.add('interacted');
      ensureFrame();
    }

    if (slowButton) slowButton.addEventListener('click', function () { startAuto('slow'); });
    if (fastButton) fastButton.addEventListener('click', function () { startAuto('fast'); });
    if (resetButton) resetButton.addEventListener('click', function () {
      stopAuto();
      inertiaVel = 0;
      speedShown = 0;
      inertiaShown = 0;
      target = 0;
      lastWheelAt = 0;
      renderHud();
      ensureFrame();
    });

    window.addEventListener('resize', function () {
      target = clamp(target, 0, maxScroll());
      updateThumb();
    }, { passive: true });

    content.style.transform = 'translate3d(0,0px,0)';
    renderHud();
    updateThumb();
  }

  if (reduceQuery.addEventListener) {
    reduceQuery.addEventListener('change', function (event) {
      isReduced = event.matches;
      if (isReduced) {
        reveals.forEach(function (el) { el.classList.add('visible'); });
      }
    });
  }

  window.__wmfReady = true;
})();

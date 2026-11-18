'use strict';
(() => {
  const bridge = window.openGridMenu;
  const actions = document.getElementById('actions'), status = document.getElementById('status');
  const close = document.getElementById('close'), previous = document.getElementById('previous'), next = document.getElementById('next');
  const cursor = document.getElementById('head-cursor');
  let state = {actions: [], busy: true}, page = 0, pending = false;
  const PAGE_SIZE = 6;
  const safeActions = () => Array.isArray(state.actions) ? state.actions.filter(action => action && typeof action.id === 'string' && typeof action.label === 'string') : [];
  const controls = () => safeActions().filter(action => action.id !== 'close');
  function setMessage(message, error = false) {
    status.textContent = typeof message === 'string' ? message : '';
    status.classList.toggle('error', error);
  }
  async function run(id) {
    const action = safeActions().find(item => item.id === id);
    if (!action || action.disabled || state.busy || pending) return;
    pending = true; render();
    try {
      const result = await bridge.runAction(id);
      setMessage(result?.message || 'Action applied.', result?.success === false || result?.ok === false);
    } catch (error) { setMessage(error?.message || 'Action unavailable.', true); }
    finally { pending = false; render(); }
  }
  function render() {
    const list = controls(), pages = Math.max(1, Math.ceil(list.length / PAGE_SIZE));
    page = Math.min(page, pages - 1);
    const shown = list.slice(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
    // Keep existing button nodes during bounded state refresh so native hover remains stable.
    const keep = new Set(shown.map(action => action.id));
    for (const button of Array.from(actions.children)) if (!keep.has(button.dataset.action)) button.remove();
    for (const action of shown) {
      let button = Array.from(actions.children).find(node => node.dataset.action === action.id);
      if (!button) {
        button = document.createElement('button'); button.className = 'action'; button.dataset.action = action.id;
        button.append(document.createElement('span'), document.createElement('span'));
        button.children[0].className = 'action-label'; button.children[1].className = 'action-detail';
        button.addEventListener('click', () => run(action.id)); actions.append(button);
      }
      button.setAttribute('aria-label', action.label); button.children[0].textContent = action.label;
      button.children[1].textContent = action.disabled ? action.reason || 'Unavailable' : action.detail || '';
      button.disabled = !!action.disabled || !!state.busy || pending;
    }
    const closing = safeActions().find(action => action.id === 'close');
    close.disabled = !closing || !!closing.disabled || !!state.busy || pending;
    previous.disabled = page === 0 || !!state.busy || pending; next.disabled = page + 1 >= pages || !!state.busy || pending;
    document.getElementById('page-number').textContent = `${page + 1} / ${pages}`;
  }
  close.addEventListener('click', () => run('close'));
  previous.addEventListener('click', () => { if (!previous.disabled) { page--; render(); } });
  next.addEventListener('click', () => { if (!next.disabled) { page++; render(); } });
  if (!bridge) { setMessage('Local controls unavailable.', true); render(); return; }
  bridge.onState(value => {
    if (!value || typeof value !== 'object') return;
    state = value;
    document.getElementById('profile').textContent = [state.profileName, state.game].filter(value => typeof value === 'string' && value).join(' · ') || 'Saved profile';
    setMessage(state.busy ? state.message || 'Applying action…' : state.message || 'Ready');
    const pointer = state.pointer;
    const valid = pointer?.valid === true && Number.isFinite(pointer.u) && Number.isFinite(pointer.v) && pointer.u >= 0 && pointer.u <= 1 && pointer.v >= 0 && pointer.v <= 1;
    cursor.hidden = !valid;
    if (valid) { cursor.style.left = `${pointer.u * 100}%`; cursor.style.top = `${pointer.v * 100}%`; }
    render();
  });
  render();
})();

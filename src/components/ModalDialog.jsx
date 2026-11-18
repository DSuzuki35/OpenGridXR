import { useLayoutEffect, useRef } from "react";
import "./ModalDialog.css";

// Shared native <dialog> wrapper. Mount it while the dialog should be open and
// unmount it to close. showModal() supplies the top layer, background inertness
// and focus containment; this wrapper adds the app rules on top:
// - initial focus on [data-autofocus] (falls back to the native first focusable),
// - Escape / close requests call onClose unless `busy`,
// - optional outside click (pointer down on the dialog's own padding area),
// - focus returns to the element that opened the dialog,
// - tall content scrolls inside the dialog instead of being clipped.
export default function ModalDialog({
  onClose,
  busy = false,
  closeOnOutsideClick = true,
  labelledBy,
  label,
  className = "",
  panelClassName = "",
  children,
}) {
  const ref = useRef(null);
  const state = useRef({ onClose, busy });
  state.current = { onClose, busy };

  useLayoutEffect(() => {
    const dialog = ref.current;
    if (!dialog) return;
    const opener = window.document.activeElement;
    let closing = false;
    const requestClose = () => {
      if (!state.current.busy) state.current.onClose?.();
    };
    const keydown = (event) => {
      if (event.key !== "Escape" || event.isComposing) return;
      // Handle Escape here so the browser's close request cannot close the
      // native dialog behind React's back, and so page-level Escape handlers
      // (for example the wide contour editor) do not also react.
      event.preventDefault();
      event.stopPropagation();
      requestClose();
    };
    const cancel = (event) => {
      // Close requests that do not originate from a focused descendant.
      event.preventDefault();
      requestClose();
    };
    const closed = () => {
      if (closing) return;
      // Something closed the native dialog unexpectedly; keep React in charge.
      if (state.current.busy) {
        if (dialog.isConnected && !dialog.open) dialog.showModal();
      } else state.current.onClose?.();
    };
    dialog.addEventListener("keydown", keydown);
    dialog.addEventListener("cancel", cancel);
    dialog.addEventListener("close", closed);
    if (!dialog.open) dialog.showModal();
    focusInitial(dialog);
    return () => {
      closing = true;
      dialog.removeEventListener("keydown", keydown);
      dialog.removeEventListener("cancel", cancel);
      dialog.removeEventListener("close", closed);
      if (dialog.open) dialog.close();
      // Restore after the commit finishes so attributes such as `inert` on the
      // opener's container have already been removed.
      queueMicrotask(() => {
        const active = window.document.activeElement;
        const lost =
          !active ||
          active === window.document.body ||
          !active.isConnected ||
          dialog.contains(active);
        if (lost && opener instanceof HTMLElement && opener.isConnected)
          opener.focus({ preventScroll: true });
      });
    };
  }, []);

  // Busy makes the content inert, which drops focus to <body>. Put it back
  // when the operation completes so keyboard users are not stranded.
  useLayoutEffect(() => {
    const dialog = ref.current;
    if (busy || !dialog?.open) return;
    const active = window.document.activeElement;
    if (!dialog.contains(active)) focusInitial(dialog);
  }, [busy]);

  return (
    <dialog
      ref={ref}
      className={`app-dialog ${className}`}
      aria-labelledby={labelledBy}
      aria-label={labelledBy ? undefined : label}
      aria-busy={busy || undefined}
      onMouseDown={(event) => {
        if (!closeOnOutsideClick || state.current.busy) return;
        const dialog = ref.current;
        if (event.target !== dialog) return;
        // Ignore presses on the dialog's own scrollbar.
        const box = dialog.getBoundingClientRect();
        if (
          event.clientX >= box.left + dialog.clientWidth ||
          event.clientY >= box.top + dialog.clientHeight
        )
          return;
        event.preventDefault();
        state.current.onClose?.();
      }}
    >
      <section className={`modal ${panelClassName}`} inert={busy || undefined}>
        {children}
      </section>
    </dialog>
  );
}

function focusInitial(dialog) {
  const target = dialog.querySelector("[data-autofocus]");
  if (target instanceof HTMLElement && !target.matches(":disabled")) {
    target.focus({ preventScroll: true });
    return;
  }
  if (dialog.contains(window.document.activeElement)) return;
  const first = dialog.querySelector(
    'button:not(:disabled), [href], input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex]:not([tabindex="-1"])',
  );
  if (first instanceof HTMLElement) first.focus({ preventScroll: true });
}

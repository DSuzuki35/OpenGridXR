import React, { createContext, useContext } from "react";
import { Gamepad2 } from "lucide-react";
import { isBrowserPreview } from "../api.mjs";
export const Context = createContext(null);
export const useInputBindings = () => useContext(Context);
const DIRECTIONS = [
  "Up",
  "Up-right",
  "Right",
  "Down-right",
  "Down",
  "Down-left",
  "Left",
  "Up-left",
];
export function describeInput(binding) {
  return binding.kind === "pov"
    ? `POV ${binding.index + 1} ${DIRECTIONS[binding.direction]}`
    : `Button ${binding.index + 1}`;
}
export function BindingButton({ actionId, disabled = false }) {
  const context = useInputBindings();
  if (!context || !actionId) return null;
  const action = context.actions.find((item) => item.id === actionId);
  if (!action) return null;
  const binding = context.settings.bindings.find(
    (item) => item.actionId === actionId,
  );
  return (
    <button
      type="button"
      className={`binding-button ${binding ? "bound" : ""}`}
      aria-label={`Bind controller: ${action?.label || actionId}`}
      title={
        binding
          ? `${binding.deviceName} - ${describeInput(binding)}. Click to relearn.`
          : "Bind a wheel, joystick or buttonbox input"
      }
      disabled={
        disabled || context.busy || action?.disabled || isBrowserPreview
      }
      onClick={() => context.learn(actionId)}
    >
      <Gamepad2 size={13} />
    </button>
  );
}

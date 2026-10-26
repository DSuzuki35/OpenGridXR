import React, { useId } from "react";
import { Info, Minus, Plus } from "lucide-react";
import { BindingButton, useInputBindings } from "./InputBindings.jsx";

export function Button({
  children,
  icon: Icon,
  variant = "secondary",
  className = "",
  ...props
}) {
  return (
    <button className={`button ${variant} ${className}`} {...props}>
      {Icon && <Icon size={15} strokeWidth={1.7} />}
      {children}
    </button>
  );
}

export function Toggle({
  checked,
  onChange,
  label,
  description,
  disabled = false,
  bindingAction,
}) {
  const id = useId();
  const bindings = useInputBindings();
  return (
    <div className={`toggle-row ${disabled ? "disabled" : ""}`}>
      <label htmlFor={id}>
        <span>{label}</span>
        {description && <small>{description}</small>}
      </label>
      <div className="toggle-actions">
        <BindingButton actionId={bindingAction} disabled={disabled} />
        <button
          id={id}
          type="button"
          className={`switch ${checked ? "on" : ""}`}
          role="switch"
          aria-checked={checked}
          aria-label={label}
          disabled={disabled}
          onClick={() =>
            bindingAction &&
            bindings?.actions.some((action) => action.id === bindingAction)
              ? bindings.toggle(bindingAction)
              : onChange(!checked)
          }
        >
          <span />
        </button>
      </div>
    </div>
  );
}

export function Range({
  label,
  value,
  onChange,
  min = 0,
  max = 100,
  step = 1,
  unit = "%",
  hint,
  disabled = false,
}) {
  const id = useId();
  const set = (raw) => {
    const parsed = Number(raw);
    if (Number.isFinite(parsed)) onChange(Math.min(max, Math.max(min, parsed)));
  };
  return (
    <div className={`range-field ${disabled ? "disabled" : ""}`}>
      <div className="range-label">
        <label htmlFor={id}>{label}</label>
        <div className="range-value">
          <input
            aria-label={`${label} value`}
            type="number"
            value={value}
            min={min}
            max={max}
            step={step}
            onChange={(event) => set(event.target.value)}
            disabled={disabled}
          />
          <span>{unit}</span>
        </div>
      </div>
      <input
        id={id}
        aria-label={label}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        disabled={disabled}
        style={{
          "--range-progress": `${((value - min) / (max - min)) * 100}%`,
        }}
        onChange={(event) => set(event.target.value)}
      />
      {hint && <small className="field-hint">{hint}</small>}
    </div>
  );
}

export function Stepper({
  label,
  value,
  onChange,
  min = 0,
  max = 40,
  unit = "%",
}) {
  return (
    <div className="stepper-field">
      <label>{label}</label>
      <div className="stepper">
        <button
          aria-label={`Decrease ${label}`}
          disabled={value <= min}
          onClick={() => onChange(Math.max(min, value - 1))}
        >
          <Minus size={12} />
        </button>
        <div>
          <input
            aria-label={label}
            type="number"
            min={min}
            max={max}
            value={value}
            onChange={(event) => {
              const n = Number(event.target.value);
              if (Number.isFinite(n)) onChange(Math.max(min, Math.min(max, n)));
            }}
          />
          <span>{unit}</span>
        </div>
        <button
          aria-label={`Increase ${label}`}
          disabled={value >= max}
          onClick={() => onChange(Math.min(max, value + 1))}
        >
          <Plus size={12} />
        </button>
      </div>
    </div>
  );
}

export function Segmented({ options, value, onChange, label, className = "" }) {
  return (
    <div className={`segmented ${className}`} role="group" aria-label={label}>
      {options.map((option) => (
        <button
          key={option.value}
          className={value === option.value ? "selected" : ""}
          aria-pressed={value === option.value}
          onClick={() => onChange(option.value)}
        >
          {option.icon && <option.icon size={14} />} {option.label}
        </button>
      ))}
    </div>
  );
}

export function Panel({
  title,
  icon: Icon,
  children,
  action,
  className = "",
  eyebrow,
  id,
}) {
  return (
    <section className={`panel ${className}`} id={id}>
      <div className="panel-heading">
        <div>
          {eyebrow && <span className="eyebrow">{eyebrow}</span>}
          <h2>
            {Icon && <Icon size={16} strokeWidth={1.6} />} {title}
          </h2>
        </div>
        {action}
      </div>
      {children}
    </section>
  );
}

export function Note({ children, tone = "muted", icon: Icon = Info }) {
  return (
    <div className={`note ${tone}`}>
      <Icon size={16} />
      <p>{children}</p>
    </div>
  );
}

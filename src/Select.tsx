import {
  useId,
  useLayoutEffect,
  useRef,
  useState,
  type KeyboardEvent,
  type ReactNode,
} from "react";
import { createPortal } from "react-dom";
import { t } from "./i18n";

type Option = { value: string; label: string; disabled?: boolean };
type Props = {
  label: string;
  value: string;
  options: Option[];
  // With values, several options can be ticked: choosing one toggles it and
  // keeps the list open, and the "" option (All …) clears them.
  values?: string[];
  onChange: (value: string) => void;
  disabled?: boolean;
  valueContent?: ReactNode;
  // Show the label inside the trigger, before the value (the other interfaces' filter chips).
  inlineLabel?: boolean;
};

/* A select-only combobox: focus stays on the trigger, and navigating the list
 * previews a choice until it is confirmed. No native OS popup on the console. */
export function Select({
  label,
  value,
  options,
  values,
  onChange,
  disabled,
  valueContent,
  inlineLabel,
}: Props) {
  const id = useId();
  const trigger = useRef<HTMLButtonElement>(null);
  const menu = useRef<HTMLDivElement>(null);
  const typed = useRef({ text: "", time: 0 });
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(value);
  const [position, setPosition] = useState({
    left: 0,
    top: 0,
    width: 0,
    maxHeight: 300,
  });
  const enabled = options.filter((option) => !option.disabled);
  const multiple = !!values;
  const ticked = (option: Option) =>
    values ? (option.value ? values.includes(option.value) : !values.length) : option.value === value;
  const selected = options.find((option) => option.value === value);
  const summary = values?.length
    ? options.filter((option) => option.value && values.includes(option.value)).map((option) => option.label).join(", ")
    : undefined;
  const highlighted =
    enabled.find((option) => option.value === active) ||
    enabled.find((option) => option.value === value) ||
    enabled[0];
  const expanded = open && !disabled && enabled.length > 0;
  const activeIndex = options.findIndex((option) => option === highlighted);

  function show() {
    if (disabled || !enabled.length) return;
    typed.current = { text: "", time: 0 };
    setActive(
      enabled.find((option) => option.value === value)?.value ||
        enabled[0].value,
    );
    setOpen(true);
  }
  function choose(option = highlighted) {
    if (multiple) {
      // Ticks or unticks it, and the list stays open for the next one.
      if (option && !option.disabled && (option.value || values!.length)) onChange(option.value);
      return;
    }
    if (option && !option.disabled && option.value !== value)
      onChange(option.value);
    setOpen(false);
  }
  function key(event: KeyboardEvent<HTMLButtonElement>) {
    // React handles the widget before the document's spatial navigation handler.
    document.documentElement.classList.remove("pointer");
    const k = event.key;
    if (k === "Tab") {
      if (expanded && !multiple) choose();
      setOpen(false);
      return;
    }
    if (k === "Escape" || k === "BrowserBack") {
      if (!expanded) return;
      event.preventDefault();
      event.stopPropagation();
      setOpen(false);
      return;
    }
    if (k === "Enter" || k === " ") {
      event.preventDefault();
      event.stopPropagation();
      if (expanded) choose();
      else show();
      return;
    }
    // A closed filter is part of the page's spatial navigation. Open it
    // explicitly with confirm (or Alt+Arrow); otherwise Up/Down must let
    // keyboard and controller users return to the game grid.
    if (!expanded && !event.altKey && (k === "ArrowDown" || k === "ArrowUp"))
      return;
    if (
      ["ArrowDown", "ArrowUp", "Home", "End", "PageDown", "PageUp"].includes(k)
    ) {
      event.preventDefault();
      event.stopPropagation();
      if (!enabled.length) return;
      if (event.altKey && k === "ArrowUp" && expanded) {
        // Alt+Up closes the list; in a multiple-choice one it never ticks.
        if (multiple) setOpen(false);
        else choose();
        return;
      }
      if (!expanded) show();
      const index = Math.max(
        0,
        expanded
          ? enabled.indexOf(highlighted)
          : enabled.findIndex((option) => option.value === value),
      );
      const next =
        k === "Home"
          ? 0
          : k === "End"
            ? enabled.length - 1
            : !expanded
              ? index
              : index +
                (k === "ArrowDown"
                  ? 1
                  : k === "ArrowUp"
                    ? -1
                    : k === "PageDown"
                      ? 10
                      : -10);
      setActive(enabled[Math.max(0, Math.min(enabled.length - 1, next))].value);
      return;
    }
    if (expanded && (k === "ArrowLeft" || k === "ArrowRight")) {
      event.preventDefault();
      event.stopPropagation();
      return;
    }
    if (k.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
      event.preventDefault();
      event.stopPropagation();
      const now = Date.now();
      const text =
        (now - typed.current.time < 700 ? typed.current.text : "") +
        k.toLocaleLowerCase();
      if (!expanded) show();
      typed.current = { text, time: now };
      const repeated = [...text].every((letter) => letter === text[0]);
      const query = repeated ? text[0] : text;
      const index = expanded && repeated ? enabled.indexOf(highlighted) + 1 : 0;
      const match = [...enabled.slice(index), ...enabled.slice(0, index)].find(
        (option) => option.label.toLocaleLowerCase().startsWith(query),
      );
      if (match) setActive(match.value);
    }
  }

  useLayoutEffect(() => {
    if (!expanded) return;
    let frame = 0;
    function place() {
      const rect = trigger.current!.getBoundingClientRect();
      if (rect.bottom < 0 || rect.top > window.innerHeight) {
        setOpen(false);
        return;
      }
      const width = Math.min(Math.max(rect.width, 220), window.innerWidth - 24);
      const below = window.innerHeight - rect.bottom - 20;
      const above = rect.top - 20;
      const flip = below < 260 && above > below;
      const maxHeight = Math.max(80, Math.min(340, flip ? above : below));
      // Measure at the final width so wrapped labels do not leave a gap above
      // the trigger when the popup opens upward on a phone.
      if (menu.current) menu.current.style.width = `${width}px`;
      const height = Math.min(
        (menu.current?.scrollHeight || 338) + 2,
        maxHeight,
      );
      setPosition({
        left: Math.max(12, Math.min(rect.left, window.innerWidth - width - 12)),
        top: flip ? Math.max(12, rect.top - height - 8) : rect.bottom + 8,
        width,
        maxHeight,
      });
    }
    function reposition() {
      if (!frame)
        frame = requestAnimationFrame(() => {
          frame = 0;
          place();
        });
    }
    function dismiss(event: Event) {
      if (
        !trigger.current?.contains(event.target as Node) &&
        !menu.current?.contains(event.target as Node)
      )
        setOpen(false);
    }
    function scroll(event: Event) {
      if (!menu.current?.contains(event.target as Node)) reposition();
    }
    place();
    document.addEventListener("pointerdown", dismiss);
    window.addEventListener("resize", reposition);
    window.addEventListener("scroll", scroll, true);
    return () => {
      document.removeEventListener("pointerdown", dismiss);
      window.removeEventListener("resize", reposition);
      window.removeEventListener("scroll", scroll, true);
      cancelAnimationFrame(frame);
    };
  }, [expanded, options.length]);

  useLayoutEffect(() => {
    const list = menu.current;
    const option = list?.querySelector<HTMLElement>('[data-active="true"]');
    if (!expanded || !list || !option) return;
    // Scroll just the popup, never the page or the trigger behind it.
    if (option.offsetTop < list.scrollTop) list.scrollTop = option.offsetTop;
    else if (
      option.offsetTop + option.offsetHeight >
      list.scrollTop + list.clientHeight
    )
      list.scrollTop =
        option.offsetTop + option.offsetHeight - list.clientHeight;
  }, [expanded, activeIndex]);

  return (
    <div className="field select-field">
      <span id={`${id}-label`}>{label}</span>
      <button
        ref={trigger}
        type="button"
        className="select-trigger"
        role="combobox"
        aria-labelledby={`${id}-label`}
        aria-haspopup="listbox"
        aria-expanded={expanded}
        aria-controls={expanded ? `${id}-list` : undefined}
        aria-activedescendant={
          expanded ? `${id}-option-${activeIndex}` : undefined
        }
        disabled={disabled || !enabled.length}
        onKeyDown={key}
        onBlur={() => setOpen(false)}
        onClick={() => (expanded ? setOpen(false) : show())}
      >
        <span>
          {inlineLabel && (
            <span className="select-inline-label" aria-hidden="true">
              {label}
            </span>
          )}
          {valueContent ??
            (summary || (multiple ? options.find((option) => !option.value)?.label : selected?.label) || t("Select an option"))}
        </span>
        <svg
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="1.7"
          strokeLinecap="round"
          strokeLinejoin="round"
          aria-hidden="true"
        >
          <path d="m7 10 5 5 5-5" />
        </svg>
      </button>
      {expanded &&
        createPortal(
          <div
            ref={menu}
            id={`${id}-list`}
            className="select-menu"
            role="listbox"
            aria-labelledby={`${id}-label`}
            aria-multiselectable={multiple || undefined}
            style={position}
          >
            {options.map((option, index) =>
              // A disabled empty choice is the trigger's placeholder, not a choice.
              option.disabled && !option.value ? null : (
                <div
                  key={option.value}
                  id={`${id}-option-${index}`}
                  role="option"
                  aria-selected={ticked(option)}
                  aria-disabled={option.disabled || undefined}
                  className="select-option"
                  data-active={option === highlighted}
                  onMouseDown={(event) => event.preventDefault()}
                  onMouseMove={() => {
                    if (!option.disabled) setActive(option.value);
                  }}
                  onClick={() => {
                    if (!option.disabled) {
                      choose(option);
                      trigger.current?.focus({ preventScroll: true });
                    }
                  }}
                >
                  <span>{option.label}</span>
                  {ticked(option) && (
                    <svg
                      viewBox="0 0 24 24"
                      fill="none"
                      stroke="currentColor"
                      strokeWidth="1.8"
                      strokeLinecap="round"
                      strokeLinejoin="round"
                      aria-hidden="true"
                    >
                      <path d="m5 12 4 4L19 6" />
                    </svg>
                  )}
                </div>
              ),
            )}
          </div>,
          document.body,
        )}
    </div>
  );
}

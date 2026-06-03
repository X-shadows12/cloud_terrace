from __future__ import annotations

from pathlib import Path
import queue
import threading
import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

from .client import CtmClient, CtmError, load_firmware_image
from .protocol import (
    CONFIG_ITEMS,
    CONTROL_MODE_LABELS,
    VALUE_ITEMS,
    CAN_BAUDRATE_CONFIG_VALUES,
    ControlMode,
    unpack_float,
)
from .zlg_can import DEVICE_TYPES, ZlgCanBus, ZlgCanError


OUTER_BG = "#efefef"
TOPBAR_BG = "#d8dad9"
TOPBAR_ACTIVE_BG = "#cfd2d1"
PAGE_BG = "#ffffff"
PANEL_BG = "#ffffff"
PANEL_LINE = "#d3d6d6"
FIELD_BG = "#f3f4f3"
BUTTON_BG = "#e6e6e6"
BUTTON_ACTIVE_BG = "#d5d5d5"
TEXT_FG = "#202020"
MUTED_FG = "#666666"
ACCENT = "#8a9500"
BLUE = "#35a8d4"
GREEN = "#1eb536"
RED = "#c94a43"
FONT_FAMILY = "Microsoft YaHei UI"
MONO_FAMILY = "Consolas"

OFFSET_LUT_NUM = 128
COGGING_MAP_NUM = 5000


class PlotCanvas(tk.Canvas):
    def __init__(
        self,
        parent,
        *,
        height: int = 170,
        line_color: str = BLUE,
        x_mode: str = "natural",
        x_span: int | None = None,
    ) -> None:
        super().__init__(
            parent,
            height=height,
            bg=PANEL_BG,
            bd=0,
            highlightthickness=1,
            highlightbackground=PANEL_LINE,
            relief=tk.FLAT,
        )
        self.values: list[float | None] = []
        self.line_color = line_color
        self.x_mode = x_mode
        self.x_span = x_span
        self.y_min: float | None = None
        self.y_max: float | None = None
        self.bind("<Configure>", lambda _event: self.redraw())

    def set_data(
        self,
        values: list[float | int | None],
        *,
        x_mode: str | None = None,
        x_span: int | None = None,
        y_min: float | None = None,
        y_max: float | None = None,
    ) -> None:
        self.values = [None if value is None else float(value) for value in values]
        if x_mode is not None:
            self.x_mode = x_mode
        if x_span is not None:
            self.x_span = x_span
        self.y_min = y_min
        self.y_max = y_max
        self.redraw()

    def redraw(self) -> None:
        self.delete("all")
        width = max(self.winfo_width(), 120)
        height = max(self.winfo_height(), 90)
        left, right, top, bottom = 58, 18, 16, 30
        plot_w = max(width - left - right, 20)
        plot_h = max(height - top - bottom, 20)

        data = [value for value in self.values if value is not None]
        if data:
            y_min = min(data) if self.y_min is None else self.y_min
            y_max = max(data) if self.y_max is None else self.y_max
            span = y_max - y_min
            if abs(span) < 1e-9:
                pad = max(abs(y_max) * 0.1, 1.0)
                y_min -= pad
                y_max += pad
            elif self.y_min is None and self.y_max is None:
                pad = span * 0.08
                y_min -= pad
                y_max += pad
        else:
            y_min, y_max = -1.0, 1.0

        y_span = y_max - y_min
        if abs(y_span) < 1e-9:
            y_span = 1.0

        self.create_rectangle(0, 0, width, height, fill=PANEL_BG, outline="")
        for tick in range(6):
            ratio = tick / 5
            y = top + plot_h * ratio
            value = y_max - y_span * ratio
            self.create_line(left, y, width - right, y, fill="#e4e4e4")
            self.create_text(left - 8, y, text=self._format_y(value), anchor=tk.E, fill=MUTED_FG, font=(MONO_FAMILY, 9))

        x_span = max(int(self.x_span or max(len(self.values) - 1, 1)), 1)
        for tick in range(9):
            ratio = tick / 8
            x = left + plot_w * ratio
            self.create_line(x, top, x, top + plot_h, fill="#ededed")
            label = self._format_x(ratio, x_span)
            self.create_text(x, top + plot_h + 12, text=label, anchor=tk.N, fill=MUTED_FG, font=(MONO_FAMILY, 9))

        points: list[float] = []
        count = max(len(self.values) - 1, 1)
        for index, value in enumerate(self.values):
            if value is None:
                continue
            x = left + plot_w * index / count
            y = top + plot_h * (y_max - value) / y_span
            points.extend([x, y])

        if len(points) >= 4:
            self.create_line(*points, fill=self.line_color, width=2)

    def _format_y(self, value: float) -> str:
        if abs(value) >= 100:
            return f"{value:.1f}"
        if abs(value) >= 10:
            return f"{value:.1f}"
        if abs(value) >= 1:
            return f"{value:.2f}"
        return f"{value:.3f}"

    def _format_x(self, ratio: float, span: int) -> str:
        if self.x_mode == "negative":
            value = -span + span * ratio
        else:
            value = span * ratio
        return f"{int(round(value))}"


class ScopePanel(tk.Frame):
    def __init__(
        self,
        parent,
        *,
        source_items,
        default_source_key: str,
        default_samples: int = 1000,
        line_color: str = BLUE,
    ) -> None:
        super().__init__(parent, bg=PANEL_BG, bd=1, relief=tk.SOLID, highlightthickness=0)
        self.source_items = [(item.key, item.label) for item in source_items]
        self.source_by_label = {label: key for key, label in self.source_items}
        self.label_by_source = {key: label for key, label in self.source_items}
        self.history: list[float] = []
        self.paused = False

        self.value_var = tk.StringVar(value="值：-")
        self.source_var = tk.StringVar(value=self.label_by_source.get(default_source_key, self.source_items[0][1]))
        self.sample_var = tk.IntVar(value=default_samples)

        header = tk.Frame(self, bg=PANEL_BG)
        header.pack(fill=tk.X, padx=12, pady=(10, 6))
        tk.Label(header, textvariable=self.value_var, bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10)).pack(side=tk.LEFT)

        controls = tk.Frame(header, bg=PANEL_BG)
        controls.pack(side=tk.RIGHT)
        tk.Label(controls, text="源：", bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 9)).pack(side=tk.LEFT)
        self.source_combo = ttk.Combobox(
            controls,
            textvariable=self.source_var,
            values=[label for _key, label in self.source_items],
            width=12,
            state="readonly",
        )
        self.source_combo.pack(side=tk.LEFT, padx=(2, 10))
        self.source_combo.bind("<<ComboboxSelected>>", lambda _event: self.clear())

        tk.Label(controls, text="采样点数：", bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 9)).pack(side=tk.LEFT)
        ttk.Spinbox(controls, from_=10, to=5000, increment=10, textvariable=self.sample_var, width=7).pack(side=tk.LEFT, padx=(2, 10))
        self.pause_btn = tk.Button(
            controls,
            text="||",
            width=3,
            command=self._toggle_pause,
            bg=BUTTON_BG,
            fg=TEXT_FG,
            activebackground=BUTTON_ACTIVE_BG,
            relief=tk.FLAT,
            bd=1,
            highlightthickness=0,
            font=(MONO_FAMILY, 9, "bold"),
        )
        self.pause_btn.pack(side=tk.LEFT)

        self.plot = PlotCanvas(self, height=168, line_color=line_color, x_mode="negative", x_span=default_samples)
        self.plot.pack(fill=tk.X, expand=True, padx=12, pady=(0, 12))

    def selected_source_key(self) -> str:
        return self.source_by_label.get(self.source_var.get(), self.source_items[0][0])

    def update_values(self, values: dict[str, float]) -> None:
        key = self.selected_source_key()
        if key not in values:
            self.value_var.set("值：-")
            return

        value = float(values[key])
        self.value_var.set(f"值：{value:.6f}")
        if self.paused:
            return

        self.history.append(value)
        self._trim_history()
        self.plot.set_data(self.history, x_mode="negative", x_span=max(self._sample_count(), 1))

    def clear(self) -> None:
        self.history.clear()
        self.plot.set_data([], x_mode="negative", x_span=max(self._sample_count(), 1))

    def _trim_history(self) -> None:
        limit = max(self._sample_count(), 1)
        if len(self.history) > limit:
            self.history = self.history[-limit:]

    def _sample_count(self) -> int:
        try:
            return int(self.sample_var.get())
        except (tk.TclError, ValueError):
            return 1000

    def _toggle_pause(self) -> None:
        self.paused = not self.paused
        self.pause_btn.configure(text="▶" if self.paused else "||")


class ScrollablePage(tk.Frame):
    def __init__(self, parent) -> None:
        super().__init__(parent, bg=PAGE_BG)
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(self, bg=PAGE_BG, bd=0, highlightthickness=0)
        self.scrollbar = ttk.Scrollbar(self, orient=tk.VERTICAL, command=self.canvas.yview)
        self.content = tk.Frame(self.canvas, bg=PAGE_BG)
        self.window_id = self.canvas.create_window((0, 0), window=self.content, anchor="nw")

        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.scrollbar.grid(row=0, column=1, sticky="ns")

        self.content.bind("<Configure>", self._on_content_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        self.canvas.bind("<Enter>", self._bind_mousewheel)
        self.canvas.bind("<Leave>", self._unbind_mousewheel)

    def _on_content_configure(self, _event=None) -> None:
        self._sync_canvas_window()

    def _on_canvas_configure(self, _event=None) -> None:
        self._sync_canvas_window()

    def _sync_canvas_window(self) -> None:
        canvas_width = max(self.canvas.winfo_width(), 1)
        canvas_height = max(self.canvas.winfo_height(), 1)
        content_height = max(self.content.winfo_reqheight(), canvas_height)
        self.canvas.itemconfigure(self.window_id, width=canvas_width, height=content_height)
        self.canvas.configure(scrollregion=self.canvas.bbox(self.window_id))

    def _bind_mousewheel(self, _event=None) -> None:
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)
        self.canvas.bind_all("<Button-4>", self._on_linux_mousewheel)
        self.canvas.bind_all("<Button-5>", self._on_linux_mousewheel)

    def _unbind_mousewheel(self, _event=None) -> None:
        self.canvas.unbind_all("<MouseWheel>")
        self.canvas.unbind_all("<Button-4>")
        self.canvas.unbind_all("<Button-5>")

    def _on_mousewheel(self, event) -> str:
        step = max(1, abs(event.delta) // 120)
        self.canvas.yview_scroll(-step if event.delta > 0 else step, "units")
        return "break"

    def _on_linux_mousewheel(self, event) -> str:
        self.canvas.yview_scroll(-1 if event.num == 4 else 1, "units")
        return "break"


class CtmHostApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("ctm_tool")
        self._configure_window_size()
        self.configure(bg=OUTER_BG)

        self.bus: ZlgCanBus | None = None
        self.client: CtmClient | None = None
        self.polling = False
        self.worker_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.config_vars: dict[int, tk.StringVar] = {}
        self.value_vars: dict[int, tk.StringVar] = {}
        self.realtime_values: dict[str, float] = {}
        self.nav_buttons: dict[str, tk.Button] = {}
        self.page_containers: dict[str, ScrollablePage] = {}
        self.pages: dict[str, tk.Frame] = {}
        self.tx_err_count = 0
        self.rx_err_count = 0

        self.dll_var = tk.StringVar(value="ControlCAN.dll")
        self.device_type_var = tk.IntVar(value=3)
        self.device_index_var = tk.IntVar(value=0)
        self.channel_var = tk.IntVar(value=0)
        self.bitrate_var = tk.StringVar(value="500K")
        self.node_id_var = tk.IntVar(value=1)
        self.axis_var = tk.StringVar(value="Left")
        self.connection_state_var = tk.StringVar(value="disconnected")
        self.tx_err_var = tk.StringVar(value="0")
        self.rx_err_var = tk.StringVar(value="0")

        self._build_style()
        self._build_layout()
        self.after(100, self._drain_worker_queue)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _configure_window_size(self) -> None:
        screen_width = self.winfo_screenwidth()
        screen_height = self.winfo_screenheight()
        width = min(1120, max(900, screen_width - 80))
        height = min(760, max(420, screen_height - 120))
        self.geometry(f"{width}x{height}")
        self.minsize(900, 420)

    def _build_style(self) -> None:
        style = ttk.Style()
        if "clam" in style.theme_names():
            style.theme_use("clam")
        elif "vista" in style.theme_names():
            style.theme_use("vista")

        default_font = tkfont.nametofont("TkDefaultFont")
        default_font.configure(family=FONT_FAMILY, size=10)
        self.option_add("*Font", default_font)
        try:
            tkfont.nametofont("TkTextFont").configure(family=FONT_FAMILY, size=10)
            tkfont.nametofont("TkMenuFont").configure(family=FONT_FAMILY, size=10)
            tkfont.nametofont("TkHeadingFont").configure(family=FONT_FAMILY, size=10, weight="bold")
        except tk.TclError:
            pass

        style.configure(".", background=PAGE_BG, foreground=TEXT_FG)
        style.configure("TFrame", background=PAGE_BG)
        style.configure("TLabel", background=PAGE_BG, foreground=TEXT_FG)
        style.configure("TButton", padding=(10, 5))
        style.configure("Green.Horizontal.TProgressbar", troughcolor="#e8ece8", background="#12a51e", thickness=18)
        style.configure("Treeview", background=PANEL_BG, fieldbackground=PANEL_BG, foreground=TEXT_FG, rowheight=28)
        style.configure("Treeview.Heading", background="#e7e7e7", foreground=TEXT_FG, relief="flat", font=(FONT_FAMILY, 10, "bold"))
        style.map("Treeview", background=[("selected", "#d6e7ff")], foreground=[("selected", TEXT_FG)])

    def _build_layout(self) -> None:
        shell = tk.Frame(self, bg=PAGE_BG, bd=0, relief=tk.FLAT)
        shell.pack(fill=tk.BOTH, expand=True, padx=12, pady=12)

        self._build_topbar(shell)
        tk.Frame(shell, bg=PANEL_LINE, height=1).pack(fill=tk.X)

        self.page_stack = tk.Frame(shell, bg=PAGE_BG)
        self.page_stack.pack(fill=tk.BOTH, expand=True)
        self.page_stack.rowconfigure(0, weight=1)
        self.page_stack.columnconfigure(0, weight=1)

        for name in ("debug", "calib", "config", "dfu", "about"):
            page_container = ScrollablePage(self.page_stack)
            page_container.grid(row=0, column=0, sticky="nsew")
            self.page_containers[name] = page_container
            self.pages[name] = page_container.content

        self._build_debug_page(self.pages["debug"])
        self._build_calib_page(self.pages["calib"])
        self._build_config_page(self.pages["config"])
        self._build_dfu_page(self.pages["dfu"])
        self._build_about_page(self.pages["about"])

        tk.Frame(shell, bg=PANEL_LINE, height=1).pack(fill=tk.X)
        self._build_status_bar(shell)
        self._show_page("debug")

    def _build_topbar(self, parent: tk.Frame) -> None:
        topbar = tk.Frame(parent, bg=TOPBAR_BG, height=48)
        topbar.pack(fill=tk.X)
        topbar.pack_propagate(False)

        nav = tk.Frame(topbar, bg=TOPBAR_BG)
        nav.pack(side=tk.LEFT, padx=8)
        tabs = [
            ("debug", "◉ 调试"),
            ("calib", "◎ 校准"),
            ("config", "⚙ 配置"),
            ("dfu", "▣ 固件"),
            ("about", "ⓘ 关于"),
        ]
        for name, text in tabs:
            btn = tk.Button(
                nav,
                text=text,
                command=lambda page=name: self._show_page(page),
                bg=TOPBAR_BG,
                fg=TEXT_FG,
                activebackground=TOPBAR_ACTIVE_BG,
                activeforeground=TEXT_FG,
                relief=tk.FLAT,
                bd=0,
                highlightthickness=0,
                padx=8,
                pady=7,
                font=(FONT_FAMILY, 11, "bold"),
            )
            btn.pack(side=tk.LEFT, padx=(0, 6), pady=6)
            self.nav_buttons[name] = btn

        conn = tk.Frame(topbar, bg=TOPBAR_BG)
        conn.pack(side=tk.RIGHT, padx=10)
        tk.Label(conn, text="节点ID：", bg=TOPBAR_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10, "bold")).pack(side=tk.LEFT)
        ttk.Spinbox(conn, from_=1, to=30, textvariable=self.node_id_var, width=5).pack(side=tk.LEFT, padx=(0, 10))
        tk.Label(conn, text="Axis:", bg=TOPBAR_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10, "bold")).pack(side=tk.LEFT)
        axis_combo = ttk.Combobox(conn, textvariable=self.axis_var, values=("Left", "Right", "Broadcast"), width=10, state="readonly")
        axis_combo.pack(
            side=tk.LEFT, padx=(0, 12)
        )
        axis_combo.bind("<<ComboboxSelected>>", lambda _event: self._sync_client_target())
        tk.Label(conn, text="波特率：", bg=TOPBAR_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10, "bold")).pack(side=tk.LEFT)
        ttk.Combobox(conn, textvariable=self.bitrate_var, values=list(CAN_BAUDRATE_CONFIG_VALUES), width=8, state="readonly").pack(
            side=tk.LEFT, padx=(0, 12)
        )
        self.connect_btn = tk.Button(
            conn,
            text="连接",
            command=self._toggle_connection,
            bg=TOPBAR_BG,
            fg=TEXT_FG,
            activebackground=TOPBAR_ACTIVE_BG,
            relief=tk.FLAT,
            bd=0,
            highlightthickness=0,
            padx=8,
            pady=7,
            font=(FONT_FAMILY, 10, "bold"),
        )
        self.connect_btn.pack(side=tk.LEFT)

    def _build_status_bar(self, parent: tk.Frame) -> None:
        bar = tk.Frame(parent, bg="#efefef", height=28)
        bar.pack(fill=tk.X)
        bar.pack_propagate(False)
        right = tk.Frame(bar, bg="#efefef")
        right.pack(side=tk.RIGHT, padx=28)
        tk.Label(right, text="CAN State:", bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(side=tk.LEFT)
        tk.Label(right, textvariable=self.connection_state_var, bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(
            side=tk.LEFT, padx=(6, 34)
        )
        tk.Label(right, text="TxErr:", bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(side=tk.LEFT)
        tk.Label(right, textvariable=self.tx_err_var, bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(side=tk.LEFT, padx=(6, 34))
        tk.Label(right, text="RxErr:", bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(side=tk.LEFT)
        tk.Label(right, textvariable=self.rx_err_var, bg="#efefef", fg=TEXT_FG, font=(MONO_FAMILY, 10)).pack(side=tk.LEFT, padx=(6, 0))

    def _show_page(self, name: str) -> None:
        self.page_containers[name].tkraise()
        self.page_containers[name].canvas.yview_moveto(0)
        for key, button in self.nav_buttons.items():
            is_active = key == name
            button.configure(
                fg=ACCENT if is_active else TEXT_FG,
                bg=TOPBAR_ACTIVE_BG if is_active else TOPBAR_BG,
                activeforeground=ACCENT if is_active else TEXT_FG,
            )

    def _flat_button(self, parent, text: str, command, *, width: int | None = None, fg: str = TEXT_FG) -> tk.Button:
        btn = tk.Button(
            parent,
            text=text,
            command=command,
            width=width,
            bg=BUTTON_BG,
            fg=fg,
            activebackground=BUTTON_ACTIVE_BG,
            activeforeground=fg,
            relief=tk.FLAT,
            bd=1,
            highlightthickness=0,
            padx=8,
            pady=4,
            font=(FONT_FAMILY, 10),
        )
        return btn

    def _build_debug_page(self, page: tk.Frame) -> None:
        self.debug_scope_top = ScopePanel(page, source_items=VALUE_ITEMS, default_source_key="position")
        self.debug_scope_top.pack(fill=tk.X, padx=10, pady=(12, 10))
        self.debug_scope_bottom = ScopePanel(page, source_items=VALUE_ITEMS, default_source_key="velocity")
        self.debug_scope_bottom.pack(fill=tk.X, padx=10, pady=(0, 12))

        bottom = tk.Frame(page, bg=PAGE_BG)
        bottom.pack(fill=tk.BOTH, expand=True, padx=12, pady=(0, 10))
        bottom.columnconfigure(0, weight=1)
        bottom.columnconfigure(1, weight=0)

        status = tk.Frame(bottom, bg=PAGE_BG)
        status.grid(row=0, column=0, sticky="nw")
        tk.Label(status, text="状态信息：", bg=PAGE_BG, fg=TEXT_FG, font=(FONT_FAMILY, 11)).grid(row=0, column=0, sticky="w", pady=(0, 10))
        self.enabled_led = tk.Label(status, text="●", bg=PAGE_BG, fg="#bdbdbd", font=(FONT_FAMILY, 12, "bold"))
        self.enabled_led.grid(row=1, column=0, sticky="w", padx=(6, 0))
        tk.Label(status, text="电机使能", bg=PAGE_BG, fg=TEXT_FG).grid(row=1, column=1, sticky="w", padx=(4, 26))
        self.target_led = tk.Label(status, text="●", bg=PAGE_BG, fg="#bdbdbd", font=(FONT_FAMILY, 12, "bold"))
        self.target_led.grid(row=1, column=2, sticky="w")
        tk.Label(status, text="目标到达", bg=PAGE_BG, fg=TEXT_FG).grid(row=1, column=3, sticky="w", padx=(4, 0))
        self.status_var = tk.StringVar(value="状态字：-")
        self.error_var = tk.StringVar(value="错误字：-")
        self.version_var = tk.StringVar(value="固件版本：-")
        tk.Label(status, textvariable=self.status_var, bg=PAGE_BG, fg=MUTED_FG).grid(row=2, column=0, columnspan=4, sticky="w", pady=(14, 0))
        tk.Label(status, textvariable=self.error_var, bg=PAGE_BG, fg=MUTED_FG).grid(row=3, column=0, columnspan=4, sticky="w", pady=(4, 0))
        tk.Label(status, textvariable=self.version_var, bg=PAGE_BG, fg=MUTED_FG).grid(row=4, column=0, columnspan=4, sticky="w", pady=(4, 0))

        controls = tk.Frame(bottom, bg=PAGE_BG)
        controls.grid(row=0, column=1, sticky="ne")
        self.axis_control_vars: dict[str, dict[str, tk.Variable]] = {}
        self._axis_control_panel(controls, "left", "左电机").grid(row=0, column=0, sticky="ew", padx=(0, 10))
        self._axis_control_panel(controls, "right", "右电机").grid(row=0, column=1, sticky="ew")

        dual = tk.Frame(controls, bg=PAGE_BG)
        dual.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self._flat_button(dual, "左右使能", lambda: self._call_axes("enable"), width=10).pack(side=tk.LEFT, fill=tk.X, expand=True)
        self._flat_button(dual, "左右失能", lambda: self._call_axes("disable"), width=10, fg=RED).pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 0)
        )
        self._flat_button(dual, "左右同步", lambda: self._call_axes("sync"), width=10).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 0))

        poll = tk.Frame(controls, bg=PAGE_BG)
        poll.grid(row=2, column=0, columnspan=2, sticky="w", pady=(8, 0))
        self.poll_interval_var = tk.IntVar(value=200)
        tk.Label(poll, text="轮询周期 ms", bg=PAGE_BG, fg=TEXT_FG).pack(side=tk.LEFT)
        ttk.Spinbox(poll, from_=50, to=5000, increment=50, textvariable=self.poll_interval_var, width=7).pack(side=tk.LEFT, padx=(6, 6))
        self.poll_btn = self._flat_button(poll, "开始轮询", self._toggle_poll, width=9)
        self.poll_btn.pack(side=tk.LEFT)
        self._flat_button(poll, "读取一次", self._read_once, width=9).pack(side=tk.LEFT, padx=(6, 0))

    def _axis_control_panel(self, parent: tk.Frame, axis: str, title: str) -> tk.Frame:
        panel = tk.Frame(parent, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        panel.columnconfigure(1, weight=1)
        tk.Label(panel, text=title, bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10, "bold")).grid(
            row=0, column=0, columnspan=4, sticky="w", padx=10, pady=(8, 4)
        )
        vars_for_axis: dict[str, tk.Variable] = {
            "mode": tk.StringVar(value=CONTROL_MODE_LABELS[ControlMode.POSITION_PROFILE]),
            "torque": tk.DoubleVar(value=0.0),
            "velocity": tk.DoubleVar(value=0.0),
            "position": tk.DoubleVar(value=0.0),
        }
        self.axis_control_vars[axis] = vars_for_axis

        tk.Label(panel, text="模式：", bg=PANEL_BG, fg=TEXT_FG).grid(row=1, column=0, sticky="e", padx=(10, 0), pady=4)
        ttk.Combobox(panel, textvariable=vars_for_axis["mode"], values=list(CONTROL_MODE_LABELS.values()), width=14, state="readonly").grid(
            row=1, column=1, columnspan=2, sticky="ew", padx=(8, 4), pady=4
        )
        self._flat_button(panel, "应用", lambda axis=axis: self._apply_axis_mode(axis), width=6).grid(
            row=1, column=3, sticky="ew", padx=(0, 8), pady=4
        )
        self._flat_button(panel, "使能", lambda axis=axis: self._call_axis(axis, "enable"), width=6).grid(
            row=2, column=0, sticky="ew", padx=(10, 4), pady=4
        )
        self._flat_button(panel, "失能", lambda axis=axis: self._call_axis(axis, "disable"), width=6, fg=RED).grid(
            row=2, column=1, sticky="ew", padx=4, pady=4
        )
        self._flat_button(panel, "清错", lambda axis=axis: self._call_axis(axis, "reset_error"), width=6).grid(
            row=2, column=2, sticky="ew", padx=4, pady=4
        )
        self._flat_button(panel, "零点", lambda axis=axis: self._call_axis(axis, "set_home"), width=6).grid(
            row=2, column=3, sticky="ew", padx=(4, 8), pady=4
        )
        self._target_row(panel, 3, "电流：", vars_for_axis["torque"], "A", lambda axis=axis: self._send_axis_torque(axis))
        self._target_row(panel, 4, "速度：", vars_for_axis["velocity"], "Turn/s", lambda axis=axis: self._send_axis_velocity(axis))
        self._target_row(panel, 5, "位置：", vars_for_axis["position"], "Turn", lambda axis=axis: self._send_axis_position(axis))
        self._flat_button(panel, "同步", lambda axis=axis: self._call_axis(axis, "sync"), width=8).grid(
            row=6, column=2, columnspan=2, sticky="ew", padx=(4, 8), pady=(6, 8)
        )
        return panel

    def _target_row(self, parent: tk.Frame, row: int, label: str, var: tk.DoubleVar, unit: str, command) -> None:
        tk.Label(parent, text=label, bg=PANEL_BG, fg=TEXT_FG).grid(row=row, column=0, sticky="e", padx=(10, 0), pady=4)
        ttk.Entry(parent, textvariable=var, width=10).grid(row=row, column=1, sticky="ew", padx=(8, 4), pady=4)
        tk.Label(parent, text=unit, bg=PANEL_BG, fg=TEXT_FG).grid(row=row, column=2, sticky="w", padx=(0, 4), pady=4)
        self._flat_button(parent, "发送", command, width=6).grid(row=row, column=3, sticky="ew", padx=(0, 8), pady=4)

    def _build_calib_page(self, page: tk.Frame) -> None:
        page.columnconfigure(0, weight=1)
        tk.Label(page, text="电机和编码器校准：", bg=PAGE_BG, fg=TEXT_FG, font=(FONT_FAMILY, 11)).pack(anchor="w", padx=14, pady=(14, 6))
        motor_panel = tk.Frame(page, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        motor_panel.pack(fill=tk.X, padx=14, pady=(0, 14))
        motor_panel.columnconfigure(0, weight=1)

        motor_body = tk.Frame(motor_panel, bg=PANEL_BG)
        motor_body.pack(fill=tk.X, padx=18, pady=18)
        motor_body.columnconfigure(0, weight=1)
        self.calib_lut_values: list[float | None] = [None] * OFFSET_LUT_NUM
        self.calib_plot = PlotCanvas(motor_body, height=210, line_color=BLUE, x_mode="natural", x_span=OFFSET_LUT_NUM)
        self.calib_plot.grid(row=0, column=0, sticky="ew", padx=(0, 16))

        info = tk.Frame(motor_body, bg=PANEL_BG)
        info.grid(row=0, column=1, sticky="ne")
        self.calib_r_var = tk.StringVar(value="-")
        self.calib_l_var = tk.StringVar(value="-")
        self.calib_pp_var = tk.StringVar(value="-")
        self.calib_dir_var = tk.StringVar(value="-")
        self.calib_offset_var = tk.StringVar(value="-")
        self._readout(info, 0, "电机相电阻(R)：", self.calib_r_var)
        self._readout(info, 1, "电机相电感(L)：", self.calib_l_var)
        self._readout(info, 2, "电机磁极对数：", self.calib_pp_var)
        self._readout(info, 3, "编码器方向：", self.calib_dir_var)
        self._readout(info, 4, "编码器偏移：", self.calib_offset_var)
        btns = tk.Frame(info, bg=PANEL_BG)
        btns.grid(row=5, column=0, columnspan=2, sticky="e", pady=(18, 0))
        self._flat_button(btns, "开始校准", self._start_calibration, width=10).pack(side=tk.LEFT)
        self._flat_button(btns, "中止", lambda: self._call_client("abort_calibration"), width=6, fg=RED).pack(side=tk.LEFT, padx=(8, 0))

        tk.Label(page, text="电机齿槽转矩补偿校准：", bg=PAGE_BG, fg=TEXT_FG, font=(FONT_FAMILY, 11)).pack(anchor="w", padx=14, pady=(0, 6))
        cogging_panel = tk.Frame(page, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        cogging_panel.pack(fill=tk.BOTH, expand=True, padx=14, pady=(0, 14))
        cogging_panel.columnconfigure(0, weight=1)
        self.anticogging_values: list[float | None] = [None] * COGGING_MAP_NUM
        self.anticogging_plot = PlotCanvas(cogging_panel, height=190, line_color=BLUE, x_mode="natural", x_span=COGGING_MAP_NUM)
        self.anticogging_plot.pack(fill=tk.BOTH, expand=True, padx=18, pady=(18, 10))
        bottom = tk.Frame(cogging_panel, bg=PANEL_BG)
        bottom.pack(fill=tk.X, padx=18, pady=(0, 12))
        self.calib_status_var = tk.StringVar(value="等待固件回报")
        tk.Label(bottom, textvariable=self.calib_status_var, bg=PANEL_BG, fg=MUTED_FG).pack(side=tk.LEFT)
        self._flat_button(bottom, "中止", lambda: self._call_client("abort_anticogging"), width=6, fg=RED).pack(side=tk.RIGHT, padx=(8, 0))
        self._flat_button(bottom, "开始齿槽补偿", self._start_anticogging, width=12).pack(side=tk.RIGHT)

    def _readout(self, parent: tk.Frame, row: int, label: str, var: tk.StringVar) -> None:
        tk.Label(parent, text=label, bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 10)).grid(row=row, column=0, sticky="w", pady=5)
        tk.Label(parent, textvariable=var, bg=PANEL_BG, fg=TEXT_FG, font=(MONO_FAMILY, 10, "bold")).grid(
            row=row, column=1, sticky="e", padx=(14, 0), pady=5
        )

    def _build_config_page(self, page: tk.Frame) -> None:
        actions = tk.Frame(page, bg=PAGE_BG)
        actions.pack(fill=tk.X, padx=14, pady=(14, 8))
        self._flat_button(actions, "读取所有", self._read_all_config, width=12).pack(side=tk.LEFT, padx=(0, 10))
        self._flat_button(actions, "写入选中项", self._write_selected_config, width=12).pack(side=tk.LEFT, padx=(0, 10))
        self._flat_button(actions, "恢复默认", self._reset_defaults, width=12, fg=RED).pack(side=tk.LEFT, padx=(0, 10))
        self._flat_button(actions, "保存配置", lambda: self._call_client("save_all_config"), width=12).pack(side=tk.LEFT)

        table_frame = tk.Frame(page, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=14, pady=(0, 10))
        columns = ("idx", "name", "value", "unit", "note")
        self.config_tree = ttk.Treeview(table_frame, columns=columns, show="headings", selectmode="browse")
        self.config_tree.heading("idx", text="序号")
        self.config_tree.heading("name", text="参数")
        self.config_tree.heading("value", text="值")
        self.config_tree.heading("unit", text="单位")
        self.config_tree.heading("note", text="说明")
        self.config_tree.column("idx", width=58, anchor=tk.CENTER)
        self.config_tree.column("name", width=240)
        self.config_tree.column("value", width=150)
        self.config_tree.column("unit", width=90)
        self.config_tree.column("note", width=380)
        self.config_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.config_tree.tag_configure("odd", background="#f9f9f9")
        self.config_tree.tag_configure("even", background=PANEL_BG)
        self.config_tree.bind("<<TreeviewSelect>>", lambda _event: self._load_selected_config_to_editor())

        scrollbar = ttk.Scrollbar(table_frame, orient=tk.VERTICAL, command=self.config_tree.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.config_tree.configure(yscrollcommand=scrollbar.set)

        for item in CONFIG_ITEMS:
            self.config_vars[item.index] = tk.StringVar(value="")
            self.config_tree.insert(
                "",
                tk.END,
                iid=str(item.index),
                values=(item.index, item.label, "", item.unit, item.note),
                tags=("odd" if item.index % 2 else "even",),
            )

        editor = tk.Frame(page, bg=PAGE_BG)
        editor.pack(fill=tk.X, padx=14, pady=(0, 14))
        self.selected_config_var = tk.StringVar(value="-")
        self.selected_value_var = tk.StringVar(value="")
        tk.Label(editor, text="选中项：", bg=PAGE_BG, fg=TEXT_FG).pack(side=tk.LEFT)
        tk.Label(editor, textvariable=self.selected_config_var, bg=PAGE_BG, fg=TEXT_FG, width=32, anchor="w").pack(side=tk.LEFT)
        ttk.Entry(editor, textvariable=self.selected_value_var, width=18).pack(side=tk.LEFT, padx=(8, 8))
        self._flat_button(editor, "写入", self._write_selected_config, width=8).pack(side=tk.LEFT)

    def _build_dfu_page(self, page: tk.Frame) -> None:
        preview_frame = tk.Frame(page, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        preview_frame.pack(fill=tk.BOTH, expand=True, padx=14, pady=(14, 8))
        self.dfu_text = tk.Text(
            preview_frame,
            wrap=tk.NONE,
            bg=PANEL_BG,
            fg=TEXT_FG,
            insertbackground=TEXT_FG,
            font=(MONO_FAMILY, 10),
            relief=tk.FLAT,
            bd=0,
            padx=8,
            pady=8,
        )
        self.dfu_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        yscroll = ttk.Scrollbar(preview_frame, orient=tk.VERTICAL, command=self.dfu_text.yview)
        yscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.dfu_text.configure(yscrollcommand=yscroll.set)

        bottom = tk.Frame(page, bg=PAGE_BG)
        bottom.pack(fill=tk.X, padx=14, pady=(0, 14))
        self.fw_path_var = tk.StringVar(value="")
        ttk.Entry(bottom, textvariable=self.fw_path_var).grid(row=0, column=0, sticky="ew", padx=(0, 8))
        self._flat_button(bottom, "选择固件", self._browse_fw, width=10).grid(row=0, column=1, padx=(0, 8))
        self._flat_button(bottom, "固件升级", self._start_dfu, width=10).grid(row=0, column=2)
        bottom.columnconfigure(0, weight=1)

        self.dfu_status_var = tk.StringVar(value="固件文件未加载")
        self.dfu_progress = ttk.Progressbar(bottom, maximum=100, style="Green.Horizontal.TProgressbar")
        self.dfu_progress.grid(row=1, column=0, sticky="ew", pady=(8, 0), padx=(0, 8))
        tk.Label(bottom, textvariable=self.dfu_status_var, bg=PAGE_BG, fg=TEXT_FG).grid(row=1, column=1, columnspan=2, sticky="ew", pady=(8, 0))

    def _build_about_page(self, page: tk.Frame) -> None:
        panel = tk.Frame(page, bg=PANEL_BG, bd=1, relief=tk.SOLID)
        panel.pack(fill=tk.BOTH, expand=True, padx=14, pady=14)
        tk.Label(panel, text="ctm_tool", bg=PANEL_BG, fg=TEXT_FG, font=(FONT_FAMILY, 18, "bold")).pack(anchor="w", padx=18, pady=(18, 4))
        tk.Label(panel, text="GD32H759 版本 CTM 驱动器上位机", bg=PANEL_BG, fg=MUTED_FG, font=(FONT_FAMILY, 10)).pack(
            anchor="w", padx=18, pady=(0, 18)
        )

        settings = tk.LabelFrame(panel, text="通信参数", bg=PANEL_BG, fg=TEXT_FG, padx=12, pady=10, font=(FONT_FAMILY, 10))
        settings.pack(fill=tk.X, padx=18, pady=(0, 18))
        settings.columnconfigure(1, weight=1)
        tk.Label(settings, text="ControlCAN.dll", bg=PANEL_BG, fg=TEXT_FG).grid(row=0, column=0, sticky="w", pady=5)
        ttk.Entry(settings, textvariable=self.dll_var).grid(row=0, column=1, sticky="ew", padx=(10, 8), pady=5)
        self._flat_button(settings, "浏览", self._browse_dll, width=8).grid(row=0, column=2, pady=5)

        self.device_label_var = tk.StringVar(value=self._device_label(3))
        device_values = [f"{item.code}: {item.label}" for item in DEVICE_TYPES]
        tk.Label(settings, text="设备类型", bg=PANEL_BG, fg=TEXT_FG).grid(row=1, column=0, sticky="w", pady=5)
        device_combo = ttk.Combobox(settings, textvariable=self.device_label_var, values=device_values, width=24, state="readonly")
        device_combo.grid(row=1, column=1, sticky="w", padx=(10, 8), pady=5)
        device_combo.bind("<<ComboboxSelected>>", lambda _event: self.device_type_var.set(int(device_combo.get().split(":")[0])))

        tk.Label(settings, text="设备号", bg=PANEL_BG, fg=TEXT_FG).grid(row=2, column=0, sticky="w", pady=5)
        ttk.Spinbox(settings, from_=0, to=8, textvariable=self.device_index_var, width=6).grid(row=2, column=1, sticky="w", padx=(10, 8), pady=5)
        tk.Label(settings, text="通道", bg=PANEL_BG, fg=TEXT_FG).grid(row=3, column=0, sticky="w", pady=5)
        ttk.Spinbox(settings, from_=0, to=3, textvariable=self.channel_var, width=6).grid(row=3, column=1, sticky="w", padx=(10, 8), pady=5)

    def _browse_dll(self) -> None:
        path = filedialog.askopenfilename(title="选择 ControlCAN.dll", filetypes=[("DLL", "*.dll"), ("所有文件", "*.*")])
        if path:
            self.dll_var.set(path)

    def _browse_fw(self) -> None:
        path = filedialog.askopenfilename(title="选择固件文件", filetypes=[("固件文件", "*.bin *.hex"), ("所有文件", "*.*")])
        if path:
            self.fw_path_var.set(path)
            self._load_firmware_preview(path)

    def _load_firmware_preview(self, path: str) -> None:
        try:
            data = load_firmware_image(path)
        except Exception as exc:
            self.dfu_status_var.set(f"固件文件加载失败：{exc}")
            messagebox.showerror("固件文件加载失败", str(exc))
            return
        self.dfu_text.delete("1.0", tk.END)
        self.dfu_text.insert(tk.END, self._format_hex_dump(data))
        self.dfu_status_var.set(f"固件文件加载完成，大小 {len(data)} 字节")
        self.dfu_progress["value"] = 0

    def _format_hex_dump(self, data: bytes, width: int = 16, max_bytes: int = 4096) -> str:
        shown = data[:max_bytes]
        lines: list[str] = []
        for offset in range(0, len(shown), width):
            chunk = shown[offset : offset + width]
            hex_part = " ".join(f"{byte:02X}" for byte in chunk).ljust(width * 3 - 1)
            ascii_part = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
            lines.append(f"{offset:08X}  {hex_part}  | {ascii_part}")
        if len(shown) < len(data):
            lines.append(f"... 仅预览前 {len(shown)} 字节，共 {len(data)} 字节")
        return "\n".join(lines)

    def _toggle_connection(self) -> None:
        if self.client is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        if self.connect_btn["state"] == tk.DISABLED:
            return
        try:
            if self.bus is not None:
                self._disconnect()
            params = {
                "dll_path": self.dll_var.get(),
                "device_type": self.device_type_var.get(),
                "device_index": self.device_index_var.get(),
                "channel_index": self.channel_var.get(),
                "bitrate": self.bitrate_var.get(),
                "node_id": self.node_id_var.get(),
            }
        except Exception as exc:
            messagebox.showerror("连接失败", str(exc))
            return

        self.connect_btn.configure(state=tk.DISABLED, text="连接中")
        self.connection_state_var.set("connecting")
        self._log("正在连接 CAN 设备...")

        def worker() -> None:
            try:
                bus = ZlgCanBus(
                    dll_path=params["dll_path"],
                    device_type=params["device_type"],
                    device_index=params["device_index"],
                    channel_index=params["channel_index"],
                    bitrate=params["bitrate"],
                )
                bus.open()
                self.worker_queue.put(("connect_done", (bus, params["node_id"])))
            except Exception as exc:
                self.worker_queue.put(("connect_error", exc))

        threading.Thread(target=worker, daemon=True).start()

    def _disconnect(self) -> None:
        self.polling = False
        if hasattr(self, "poll_btn"):
            self.poll_btn.configure(text="开始轮询")
        self.connect_btn.configure(state=tk.NORMAL, text="连接")
        if self.bus is not None:
            self.bus.close()
        self.bus = None
        self.client = None
        self.connection_state_var.set("disconnected")
        self._log("已断开")

    def _require_client(self) -> CtmClient:
        if self.client is None:
            raise CtmError("尚未连接")
        self._sync_client_target()
        return self.client

    def _sync_client_target(self) -> None:
        if self.client is None:
            return
        self.client.set_base_node_id(self.node_id_var.get())
        self.client.set_target_axis(self.axis_var.get())

    def _call_client(self, method_name: str, *args) -> None:
        try:
            client = self._require_client()
            getattr(client, method_name)(*args)
            self._log(f"{method_name} 成功")
        except Exception as exc:
            self.tx_err_count += 1
            self.tx_err_var.set(str(self.tx_err_count))
            self._log(f"{method_name} 失败：{exc}")
            messagebox.showerror("命令失败", str(exc))

    def _call_axis(self, axis: str, method_name: str, *args) -> None:
        try:
            client = self._require_client()
            client.call_axis(axis, method_name, *args)
            self._log(f"{self._axis_short(axis)} {method_name} 成功")
        except Exception as exc:
            self.tx_err_count += 1
            self.tx_err_var.set(str(self.tx_err_count))
            self._log(f"{self._axis_short(axis)} {method_name} 失败：{exc}")
            messagebox.showerror("命令失败", str(exc))

    def _call_axes(self, method_name: str, *args) -> None:
        for axis in ("left", "right"):
            self._call_axis(axis, method_name, *args)

    def _apply_axis_mode(self, axis: str) -> None:
        reverse = {label: mode for mode, label in CONTROL_MODE_LABELS.items()}
        self._call_axis(axis, "set_op_mode", reverse[str(self.axis_control_vars[axis]["mode"].get())])

    def _send_axis_torque(self, axis: str) -> None:
        self._call_axis(axis, "set_torque", self.axis_control_vars[axis]["torque"].get())

    def _send_axis_velocity(self, axis: str) -> None:
        self._call_axis(axis, "set_velocity", self.axis_control_vars[axis]["velocity"].get())

    def _send_axis_position(self, axis: str) -> None:
        self._call_axis(axis, "set_position", self.axis_control_vars[axis]["position"].get())

    def _selected_read_axes(self) -> tuple[str, ...]:
        return ("left", "right")

    @staticmethod
    def _axis_short(axis: str) -> str:
        return "R" if axis == "right" else "L"

    def _read_once(self) -> None:
        try:
            client = self._require_client()
            axes = self._selected_read_axes()
            statuses = {axis: client.get_axis_statusword(axis) for axis in axes}

            self.status_var.set(
                "Status: "
                + " | ".join(
                    f"{self._axis_short(axis)} 0x{statuses[axis].status_code:02X} "
                    f"{' '.join(statuses[axis].status) or '-'}"
                    for axis in axes
                )
            )
            self.error_var.set(
                "Error: "
                + " | ".join(
                    f"{self._axis_short(axis)} 0x{statuses[axis].errors_code:02X} "
                    f"{' '.join(statuses[axis].errors) or '-'}"
                    for axis in axes
                )
            )
            self.enabled_led.configure(fg=GREEN if any(status.status_code & 0x01 for status in statuses.values()) else "#bdbdbd")
            self.target_led.configure(fg=GREEN if all(status.status_code & 0x02 for status in statuses.values()) else "#bdbdbd")
            major, minor = client.get_axis_fw_version(axes[0])
            if len(axes) == 1:
                self.version_var.set(f"FW: {major}.{minor}")
            else:
                versions = {axes[0]: (major, minor)}
                for axis in axes[1:]:
                    versions[axis] = client.get_axis_fw_version(axis)
                self.version_var.set(
                    "FW: "
                    + " | ".join(
                        f"{self._axis_short(axis)} {versions[axis][0]}.{versions[axis][1]}"
                        for axis in axes
                    )
                )

            values_by_axis: dict[str, dict[str, float]] = {}
            self.realtime_values.clear()
            for axis in axes:
                values_by_axis[axis] = {}
                for item in VALUE_ITEMS:
                    values_by_axis[axis][item.key] = client.get_axis_value(axis, item.index)

            for item in VALUE_ITEMS:
                if len(axes) == 1:
                    value = values_by_axis[axes[0]][item.key]
                    self.realtime_values[item.key] = value
                    if item.index in self.value_vars:
                        self.value_vars[item.index].set(f"{value:.5g}")
                else:
                    self.realtime_values[item.key] = sum(values_by_axis[axis][item.key] for axis in axes) / len(axes)
                    if item.index in self.value_vars:
                        self.value_vars[item.index].set(
                            " / ".join(
                                f"{self._axis_short(axis)} {values_by_axis[axis][item.key]:.5g}"
                                for axis in axes
                            )
                        )
            self.debug_scope_top.update_values(self.realtime_values)
            self.debug_scope_bottom.update_values(self.realtime_values)
        except Exception as exc:
            self.rx_err_count += 1
            self.rx_err_var.set(str(self.rx_err_count))
            self._log(f"读取失败：{exc}")
            messagebox.showerror("读取失败", str(exc))

    def _toggle_poll(self) -> None:
        self.polling = not self.polling
        self.poll_btn.configure(text="停止轮询" if self.polling else "开始轮询")
        if self.polling:
            self._poll_tick()

    def _poll_tick(self) -> None:
        if not self.polling:
            return
        try:
            self._read_once()
            if self.client is not None:
                self.client.poll_events()
        except tk.TclError:
            return
        finally:
            self.after(max(self.poll_interval_var.get(), 50), self._poll_tick)

    def _start_calibration(self) -> None:
        self.calib_lut_values = [None] * OFFSET_LUT_NUM
        self.calib_plot.set_data(self.calib_lut_values, x_mode="natural", x_span=OFFSET_LUT_NUM)
        self.calib_r_var.set("-")
        self.calib_l_var.set("-")
        self.calib_pp_var.set("-")
        self.calib_dir_var.set("-")
        self.calib_offset_var.set("-")
        self.calib_status_var.set("电机和编码器校准进行中")
        self._call_client("start_calibration")

    def _start_anticogging(self) -> None:
        self.anticogging_values = [None] * COGGING_MAP_NUM
        self.anticogging_plot.set_data(self.anticogging_values, x_mode="natural", x_span=COGGING_MAP_NUM)
        self.calib_status_var.set("齿槽补偿校准进行中")
        self._call_client("start_anticogging")

    def _read_all_config(self) -> None:
        try:
            client = self._require_client()
            for item in CONFIG_ITEMS:
                value = client.get_config(item)
                self.config_vars[item.index].set(f"{value:.8g}" if isinstance(value, float) else str(value))
                self.config_tree.set(str(item.index), "value", self.config_vars[item.index].get())
            self._log("参数读取完成")
        except Exception as exc:
            self.rx_err_count += 1
            self.rx_err_var.set(str(self.rx_err_count))
            self._log(f"读取参数失败：{exc}")
            messagebox.showerror("读取参数失败", str(exc))

    def _load_selected_config_to_editor(self) -> None:
        selected = self.config_tree.selection()
        if not selected:
            return
        index = int(selected[0])
        item = next(item for item in CONFIG_ITEMS if item.index == index)
        self.selected_config_var.set(f"{item.index}. {item.label}")
        self.selected_value_var.set(self.config_vars[index].get())

    def _write_selected_config(self) -> None:
        selected = self.config_tree.selection()
        if not selected:
            messagebox.showinfo("未选择参数", "请先选择一行参数。")
            return
        index = int(selected[0])
        item = next(item for item in CONFIG_ITEMS if item.index == index)
        value = self.selected_value_var.get().strip()
        try:
            client = self._require_client()
            client.set_config(item, value)
            self.config_vars[index].set(value)
            self.config_tree.set(str(index), "value", value)
            self._log(f"写入参数 {item.label}={value} 成功")
        except Exception as exc:
            self.tx_err_count += 1
            self.tx_err_var.set(str(self.tx_err_count))
            self._log(f"写入参数失败：{exc}")
            messagebox.showerror("写入参数失败", str(exc))

    def _reset_defaults(self) -> None:
        if not messagebox.askyesno("恢复默认", "确定要把所有参数恢复为固件默认值吗？"):
            return
        self._call_client("reset_all_config")

    def _start_dfu(self) -> None:
        path = self.fw_path_var.get().strip()
        if not path:
            messagebox.showinfo("未选择固件", "请先选择固件文件。")
            return
        try:
            client = self._require_client()
        except Exception as exc:
            messagebox.showerror("升级失败", str(exc))
            return
        self.polling = False
        if hasattr(self, "poll_btn"):
            self.poll_btn.configure(text="开始轮询")

        def worker() -> None:
            try:
                def progress(done: int, total: int) -> None:
                    self.worker_queue.put(("dfu_progress", (done, total)))

                client.dfu_update(path, progress)
                self.worker_queue.put(("dfu_done", None))
            except Exception as exc:
                self.worker_queue.put(("dfu_error", exc))

        self.dfu_progress["value"] = 0
        self.dfu_status_var.set("正在升级")
        threading.Thread(target=worker, daemon=True).start()

    def _drain_worker_queue(self) -> None:
        while True:
            try:
                kind, payload = self.worker_queue.get_nowait()
            except queue.Empty:
                break

            if kind == "dfu_progress":
                done, total = payload
                percent = 100.0 * done / total if total else 0
                self.dfu_progress["value"] = percent
                self.dfu_status_var.set(f"升级进度：{done}/{total} 字节（{percent:.1f}%）")
            elif kind == "dfu_done":
                self.dfu_progress["value"] = 100
                self.dfu_status_var.set("升级命令完成，设备应进入 bootloader 并重启")
                self._log("固件升级完成")
            elif kind == "dfu_error":
                self.tx_err_count += 1
                self.tx_err_var.set(str(self.tx_err_count))
                self.dfu_status_var.set(f"升级失败：{payload}")
                self._log(f"升级失败：{payload}")
                messagebox.showerror("升级失败", str(payload))
            elif kind == "connect_done":
                bus, node_id = payload
                self.bus = bus
                self.client = CtmClient(bus, node_id)
                self.client.on_event = self._handle_event
                self.connect_btn.configure(state=tk.NORMAL, text="断开")
                self.connection_state_var.set("normal")
                self._log("连接成功")
            elif kind == "connect_error":
                self.bus = None
                self.client = None
                self.tx_err_count += 1
                self.tx_err_var.set(str(self.tx_err_count))
                self.connect_btn.configure(state=tk.NORMAL, text="连接")
                self.connection_state_var.set("fault")
                self._log(f"连接失败：{payload}")
                messagebox.showerror("连接失败", str(payload))
        self.after(100, self._drain_worker_queue)

    def _handle_event(self, event) -> None:
        if event.kind == "calibration":
            self._handle_calibration_event(event)
        elif event.kind == "anticogging":
            self._handle_anticogging_event(event)
        else:
            self._log(f"事件 {event.kind}：{event.step} {event.value}")

    def _handle_calibration_event(self, event) -> None:
        step = int(event.step)
        value = unpack_float(event.value)
        if step == 1:
            self.calib_r_var.set(f"{value:.6f}")
        elif step == 2:
            self.calib_l_var.set(f"{value:.6f}")
        elif step == 3:
            self.calib_pp_var.set(str(int(round(value))))
        elif step == 4:
            self.calib_dir_var.set(str(int(round(value))))
        elif step == 5:
            self.calib_offset_var.set(str(int(round(value))))
        elif 10 <= step < 10 + OFFSET_LUT_NUM:
            index = step - 10
            self.calib_lut_values[index] = value
            self.calib_plot.set_data(self.calib_lut_values, x_mode="natural", x_span=OFFSET_LUT_NUM)
        self.calib_status_var.set(f"校准回报：步骤={step} 值={value:.6g}")

    def _handle_anticogging_event(self, event) -> None:
        step = int(event.step)
        if step >= COGGING_MAP_NUM:
            self.calib_status_var.set("齿槽补偿校准完成")
            self.anticogging_plot.set_data(self.anticogging_values, x_mode="natural", x_span=COGGING_MAP_NUM)
            return

        self.anticogging_values[step] = float(event.value)
        if step % 50 == 0 or step >= COGGING_MAP_NUM - 1:
            self.anticogging_plot.set_data(self.anticogging_values, x_mode="natural", x_span=COGGING_MAP_NUM)
        self.calib_status_var.set(f"齿槽补偿：索引={step} 值={event.value}")

    def _device_label(self, code: int) -> str:
        for item in DEVICE_TYPES:
            if item.code == code:
                return f"{item.code}: {item.label}"
        return str(code)

    def _log(self, text: str) -> None:
        if not hasattr(self, "log_text"):
            return
        self.log_text.insert(tk.END, text + "\n")
        self.log_text.see(tk.END)

    def _on_close(self) -> None:
        self._disconnect()
        self.destroy()


def main() -> None:
    app = CtmHostApp()
    app.mainloop()


if __name__ == "__main__":
    main()

from __future__ import annotations

from pathlib import Path
import queue
import threading
import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

from .client import CtmClient, CtmError
from .protocol import (
    CONFIG_ITEMS,
    CONTROL_MODE_LABELS,
    VALUE_ITEMS,
    CAN_BAUDRATE_CONFIG_VALUES,
    ControlMode,
)
from .zlg_can import DEVICE_TYPES, ZlgCanBus, ZlgCanError


APP_BG = "#eef2f7"
HEADER_BG = "#0f172a"
HEADER_FG = "#f8fafc"
PANEL_BG = "#ffffff"
PANEL_ALT_BG = "#f8fafc"
TEXT_FG = "#0f172a"
MUTED_FG = "#64748b"
ACCENT = "#2563eb"
ACCENT_ACTIVE = "#1d4ed8"
DANGER = "#dc2626"
LOG_BG = "#0b1020"
LOG_FG = "#dbe4ff"
FONT_FAMILY = "Microsoft YaHei UI"
MONO_FAMILY = "Consolas"


class CtmHostApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("CTM 上位机 · ZLG USB CAN")
        self.geometry("1200x800")
        self.minsize(1080, 700)
        self.configure(bg=APP_BG)

        self.bus: ZlgCanBus | None = None
        self.client: CtmClient | None = None
        self.polling = False
        self.worker_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.config_vars: dict[int, tk.StringVar] = {}
        self.value_vars: dict[int, tk.StringVar] = {}

        self._build_style()
        self._build_layout()
        self.after(100, self._drain_worker_queue)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_style(self) -> None:
        self.style = ttk.Style()
        if "clam" in self.style.theme_names():
            self.style.theme_use("clam")
        elif "vista" in self.style.theme_names():
            self.style.theme_use("vista")
        default_font = tkfont.nametofont("TkDefaultFont")
        default_font.configure(family=FONT_FAMILY, size=10)
        self.option_add("*Font", default_font)
        try:
            tkfont.nametofont("TkTextFont").configure(family=FONT_FAMILY, size=10)
            tkfont.nametofont("TkMenuFont").configure(family=FONT_FAMILY, size=10)
            tkfont.nametofont("TkHeadingFont").configure(family=FONT_FAMILY, size=10, weight="bold")
        except tk.TclError:
            pass
        self.style.configure(".", background=APP_BG, foreground=TEXT_FG)
        self.style.configure("TFrame", background=APP_BG)
        self.style.configure("TLabel", background=APP_BG, foreground=TEXT_FG)
        self.style.configure("TNotebook", background=APP_BG, borderwidth=0)
        self.style.configure("TNotebook.Tab", padding=(16, 8), font=(FONT_FAMILY, 10))
        self.style.map("TNotebook.Tab", background=[("selected", PANEL_BG)], foreground=[("selected", TEXT_FG)])
        self.style.configure("TLabelframe", background=APP_BG, borderwidth=1, relief="solid")
        self.style.configure("TLabelframe.Label", background=APP_BG, foreground=TEXT_FG, font=(FONT_FAMILY, 10, "bold"))
        self.style.configure("TButton", padding=(10, 6))
        self.style.configure("Accent.TButton", background=ACCENT, foreground="white", padding=(12, 7), borderwidth=0)
        self.style.map(
            "Accent.TButton",
            background=[("active", ACCENT_ACTIVE), ("pressed", ACCENT_ACTIVE)],
            foreground=[("disabled", "#e2e8f0")],
        )
        self.style.configure("Ghost.TButton", background="#e2e8f0", foreground=TEXT_FG, padding=(12, 7), borderwidth=0)
        self.style.map("Ghost.TButton", background=[("active", "#d6dfea"), ("pressed", "#cbd5e1")])
        self.style.configure("Danger.TButton", background="#fee2e2", foreground=DANGER, padding=(12, 7), borderwidth=0)
        self.style.map("Danger.TButton", background=[("active", "#fecaca"), ("pressed", "#fca5a5")])
        self.style.configure("Status.TLabel", font=(FONT_FAMILY, 10, "bold"))
        self.style.configure("Title.TLabel", background=HEADER_BG, foreground=HEADER_FG, font=(FONT_FAMILY, 18, "bold"))
        self.style.configure("Subtitle.TLabel", background=HEADER_BG, foreground="#cbd5e1", font=(FONT_FAMILY, 10))
        self.style.configure("Section.TLabel", background=APP_BG, foreground=TEXT_FG, font=(FONT_FAMILY, 10, "bold"))
        self.style.configure("Hint.TLabel", background=APP_BG, foreground=MUTED_FG, font=(FONT_FAMILY, 9))
        self.style.configure("MetricName.TLabel", background=APP_BG, foreground=MUTED_FG, font=(FONT_FAMILY, 9))
        self.style.configure("MetricValue.TLabel", background=APP_BG, foreground=TEXT_FG, font=(FONT_FAMILY, 14, "bold"))
        self.style.configure("MetricUnit.TLabel", background=APP_BG, foreground=MUTED_FG, font=(FONT_FAMILY, 9))
        self.style.configure("Info.TLabel", background=APP_BG, foreground=TEXT_FG, font=(FONT_FAMILY, 10))
        self.style.configure("MutedInfo.TLabel", background=APP_BG, foreground=MUTED_FG, font=(FONT_FAMILY, 9))
        self.style.configure(
            "Treeview",
            background=PANEL_BG,
            fieldbackground=PANEL_BG,
            foreground=TEXT_FG,
            rowheight=28,
            borderwidth=0,
        )
        self.style.configure(
            "Treeview.Heading",
            background="#dbe4f0",
            foreground=TEXT_FG,
            relief="flat",
            font=(FONT_FAMILY, 10, "bold"),
        )
        self.style.map("Treeview", background=[("selected", "#c7d2fe")], foreground=[("selected", TEXT_FG)])
        self.style.configure("TProgressbar", thickness=10)

    def _build_layout(self) -> None:
        root = ttk.Frame(self, padding=(16, 14, 16, 16))
        root.pack(fill=tk.BOTH, expand=True)

        self._build_header(root)
        self._build_connection_bar(root)

        self.tabs = ttk.Notebook(root)
        self.tabs.pack(fill=tk.BOTH, expand=True, pady=(12, 0))

        self.debug_tab = ttk.Frame(self.tabs, padding=10)
        self.config_tab = ttk.Frame(self.tabs, padding=10)
        self.calib_tab = ttk.Frame(self.tabs, padding=10)
        self.dfu_tab = ttk.Frame(self.tabs, padding=10)
        self.log_tab = ttk.Frame(self.tabs, padding=10)

        self.tabs.add(self.debug_tab, text="调试")
        self.tabs.add(self.config_tab, text="参数")
        self.tabs.add(self.calib_tab, text="校准")
        self.tabs.add(self.dfu_tab, text="固件升级")
        self.tabs.add(self.log_tab, text="日志")

        self._build_debug_tab()
        self._build_config_tab()
        self._build_calib_tab()
        self._build_dfu_tab()
        self._build_log_tab()

    def _build_header(self, parent: ttk.Frame) -> None:
        header = tk.Frame(parent, bg=HEADER_BG, highlightthickness=0)
        header.pack(fill=tk.X)
        header.columnconfigure(0, weight=1)
        header.columnconfigure(1, weight=0)

        left = tk.Frame(header, bg=HEADER_BG)
        left.grid(row=0, column=0, sticky="w", padx=18, pady=14)
        tk.Label(left, text="CTM 上位机", bg=HEADER_BG, fg=HEADER_FG, font=(FONT_FAMILY, 20, "bold")).pack(anchor="w")
        tk.Label(
            left,
            text="ZLG USB CAN 控制、参数、校准与固件升级",
            bg=HEADER_BG,
            fg="#cbd5e1",
            font=(FONT_FAMILY, 10),
        ).pack(anchor="w", pady=(4, 0))

        right = tk.Frame(header, bg=HEADER_BG)
        right.grid(row=0, column=1, sticky="e", padx=18, pady=14)
        tk.Label(right, text="工业电机上位机界面", bg=HEADER_BG, fg="#94a3b8", font=(FONT_FAMILY, 9)).pack(anchor="e")

    def _build_connection_bar(self, parent: ttk.Frame) -> None:
        bar = ttk.LabelFrame(parent, text="连接配置", padding=12)
        bar.pack(fill=tk.X)

        self.dll_var = tk.StringVar(value="ControlCAN.dll")
        self.device_type_var = tk.IntVar(value=3)
        self.device_index_var = tk.IntVar(value=0)
        self.channel_var = tk.IntVar(value=0)
        self.bitrate_var = tk.StringVar(value="500K")
        self.node_id_var = tk.IntVar(value=1)
        self.axis_var = tk.StringVar(value="Left")
        self.connection_state_var = tk.StringVar(value="未连接")

        bar.columnconfigure(0, weight=1)
        bar.columnconfigure(1, weight=0)

        form = ttk.Frame(bar)
        form.grid(row=0, column=0, sticky="ew")
        form.columnconfigure(1, weight=1)

        self.device_label_var = tk.StringVar(value=self._device_label(3))
        device_values = [f"{item.code}: {item.label}" for item in DEVICE_TYPES]

        ttk.Label(form, text="DLL").grid(row=0, column=0, sticky=tk.W, pady=(0, 8))
        ttk.Entry(form, textvariable=self.dll_var).grid(row=0, column=1, sticky=tk.EW, padx=(8, 12), pady=(0, 8))
        ttk.Button(form, text="浏览", style="Ghost.TButton", command=self._browse_dll).grid(row=0, column=2, sticky=tk.W, pady=(0, 8))

        ttk.Label(form, text="设备").grid(row=0, column=3, sticky=tk.W, padx=(20, 0), pady=(0, 8))
        device_combo = ttk.Combobox(form, textvariable=self.device_label_var, values=device_values, width=24, state="readonly")
        device_combo.grid(row=0, column=4, sticky=tk.W, padx=(8, 12), pady=(0, 8))
        device_combo.bind("<<ComboboxSelected>>", lambda event: self.device_type_var.set(int(device_combo.get().split(":")[0])))

        ttk.Label(form, text="设备号").grid(row=0, column=5, sticky=tk.W, padx=(8, 0), pady=(0, 8))
        ttk.Spinbox(form, from_=0, to=8, textvariable=self.device_index_var, width=5).grid(row=0, column=6, padx=(8, 12), pady=(0, 8))

        ttk.Label(form, text="通道").grid(row=0, column=7, sticky=tk.W, padx=(8, 0), pady=(0, 8))
        ttk.Spinbox(form, from_=0, to=3, textvariable=self.channel_var, width=5).grid(row=0, column=8, padx=(8, 12), pady=(0, 8))

        ttk.Label(form, text="波特率").grid(row=1, column=0, sticky=tk.W)
        ttk.Combobox(form, textvariable=self.bitrate_var, values=list(CAN_BAUDRATE_CONFIG_VALUES), width=10, state="readonly").grid(
            row=1,
            column=1,
            sticky=tk.W,
            padx=(8, 12),
        )

        ttk.Label(form, text="节点").grid(row=1, column=3, sticky=tk.W, padx=(20, 0))
        ttk.Spinbox(form, from_=1, to=30, textvariable=self.node_id_var, width=6).grid(row=1, column=4, sticky=tk.W, padx=(8, 8))
        ttk.Label(form, text="Axis").grid(row=1, column=5, sticky=tk.W, padx=(8, 0))
        axis_combo = ttk.Combobox(form, textvariable=self.axis_var, values=("Left", "Right", "Broadcast"), width=10, state="readonly")
        axis_combo.grid(
            row=1, column=6, sticky=tk.W, padx=(8, 12)
        )
        axis_combo.bind("<<ComboboxSelected>>", lambda _event: self._sync_client_target())

        self.connection_badge = tk.Label(
            form,
            textvariable=self.connection_state_var,
            bg="#e2e8f0",
            fg=TEXT_FG,
            padx=12,
            pady=4,
            font=(FONT_FAMILY, 10, "bold"),
            bd=0,
            highlightthickness=0,
        )
        self.connection_badge.grid(row=1, column=7, columnspan=2, sticky=tk.W, padx=(8, 12))

        action_bar = ttk.Frame(bar)
        action_bar.grid(row=0, column=1, rowspan=2, sticky="ne", padx=(16, 0))
        self.connect_btn = ttk.Button(action_bar, text="连接", style="Accent.TButton", command=self._connect)
        self.connect_btn.pack(fill=tk.X)
        ttk.Button(action_bar, text="断开", style="Ghost.TButton", command=self._disconnect).pack(fill=tk.X, pady=(8, 0))

    def _build_debug_tab(self) -> None:
        self.debug_tab.rowconfigure(0, weight=1)
        self.debug_tab.columnconfigure(0, weight=1)
        self.debug_tab.columnconfigure(1, weight=0)

        left = ttk.Frame(self.debug_tab)
        left.grid(row=0, column=0, sticky="nsew")
        left.columnconfigure(0, weight=1)

        right = ttk.Frame(self.debug_tab)
        right.grid(row=0, column=1, sticky="ns", padx=(14, 0))

        values = ttk.LabelFrame(left, text="实时数据", padding=12)
        values.pack(fill=tk.X)
        for index, item in enumerate(VALUE_ITEMS):
            row = index // 4
            col = index % 4
            cell = ttk.Frame(values, padding=(10, 8))
            cell.grid(row=row, column=col, sticky="ew", padx=8, pady=6)
            cell.columnconfigure(0, weight=1)
            ttk.Label(cell, text=item.label, style="MetricName.TLabel").grid(row=0, column=0, sticky="w")
            var = tk.StringVar(value="-")
            self.value_vars[item.index] = var
            ttk.Label(cell, textvariable=var, style="MetricValue.TLabel").grid(row=1, column=0, sticky="w", pady=(2, 0))
            ttk.Label(cell, text=item.unit, style="MetricUnit.TLabel").grid(row=2, column=0, sticky="w")
        for col in range(4):
            values.columnconfigure(col, weight=1)

        status_box = ttk.LabelFrame(left, text="状态摘要", padding=12)
        status_box.pack(fill=tk.X, pady=(12, 0))
        self.status_var = tk.StringVar(value="状态字：-")
        self.error_var = tk.StringVar(value="错误字：-")
        self.version_var = tk.StringVar(value="固件版本：-")
        ttk.Label(status_box, textvariable=self.status_var, style="Info.TLabel").pack(anchor=tk.W)
        ttk.Label(status_box, textvariable=self.error_var, style="Info.TLabel").pack(anchor=tk.W, pady=(4, 0))
        ttk.Label(status_box, textvariable=self.version_var, style="Info.TLabel").pack(anchor=tk.W, pady=(4, 0))

        controls = ttk.LabelFrame(right, text="电机控制", padding=12)
        controls.pack(fill=tk.X)
        self.axis_control_vars: dict[str, dict[str, tk.Variable]] = {}
        self._build_axis_control_panel(controls, "left", "左电机")
        self._build_axis_control_panel(controls, "right", "右电机")

        dual_actions = ttk.Frame(controls)
        dual_actions.pack(fill=tk.X, pady=(10, 0))
        ttk.Button(dual_actions, text="左右使能", style="Accent.TButton", command=lambda: self._call_axes("enable")).pack(
            side=tk.LEFT, fill=tk.X, expand=True
        )
        ttk.Button(dual_actions, text="左右失能", style="Danger.TButton", command=lambda: self._call_axes("disable")).pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 0)
        )
        ttk.Button(controls, text="左右同步下发", style="Accent.TButton", command=lambda: self._call_axes("sync")).pack(
            fill=tk.X, pady=(8, 0)
        )

        poll = ttk.LabelFrame(right, text="轮询", padding=12)
        poll.pack(fill=tk.X, pady=(12, 0))
        self.poll_interval_var = tk.IntVar(value=200)
        ttk.Label(poll, text="周期 ms").grid(row=0, column=0, sticky=tk.W)
        ttk.Spinbox(poll, from_=50, to=5000, increment=50, textvariable=self.poll_interval_var, width=8).grid(
            row=0,
            column=1,
            sticky=tk.EW,
            padx=(8, 0),
        )
        ttk.Button(poll, text="读取一次", style="Accent.TButton", command=self._read_once).grid(
            row=1, column=0, columnspan=2, sticky=tk.EW, pady=(8, 2)
        )
        self.poll_btn = ttk.Button(poll, text="开始轮询", style="Accent.TButton", command=self._toggle_poll)
        self.poll_btn.grid(row=2, column=0, columnspan=2, sticky=tk.EW, pady=2)
        poll.columnconfigure(1, weight=1)

    def _build_axis_control_panel(self, parent: ttk.Frame, axis: str, title: str) -> None:
        panel = ttk.LabelFrame(parent, text=title, padding=10)
        panel.pack(fill=tk.X, pady=(0, 10))
        vars_for_axis: dict[str, tk.Variable] = {
            "mode": tk.StringVar(value=CONTROL_MODE_LABELS[ControlMode.POSITION_PROFILE]),
            "torque": tk.DoubleVar(value=0.0),
            "velocity": tk.DoubleVar(value=0.0),
            "position": tk.DoubleVar(value=0.0),
        }
        self.axis_control_vars[axis] = vars_for_axis

        ttk.Label(panel, text="模式").grid(row=0, column=0, sticky=tk.W)
        ttk.Combobox(
            panel,
            textvariable=vars_for_axis["mode"],
            values=list(CONTROL_MODE_LABELS.values()),
            width=18,
            state="readonly",
        ).grid(row=0, column=1, sticky=tk.EW, padx=(8, 0), pady=2)
        ttk.Button(panel, text="应用", style="Accent.TButton", command=lambda axis=axis: self._apply_axis_mode(axis)).grid(
            row=0, column=2, sticky=tk.EW, padx=(6, 0), pady=2
        )

        ttk.Button(panel, text="使能", style="Accent.TButton", command=lambda axis=axis: self._call_axis(axis, "enable")).grid(
            row=1, column=0, sticky=tk.EW, pady=(8, 2)
        )
        ttk.Button(panel, text="失能", style="Danger.TButton", command=lambda axis=axis: self._call_axis(axis, "disable")).grid(
            row=1, column=1, sticky=tk.EW, padx=(8, 0), pady=(8, 2)
        )
        ttk.Button(panel, text="清错", style="Ghost.TButton", command=lambda axis=axis: self._call_axis(axis, "reset_error")).grid(
            row=1, column=2, sticky=tk.EW, padx=(6, 0), pady=(8, 2)
        )

        self._target_row(panel, 2, "电流 A", vars_for_axis["torque"], lambda axis=axis: self._send_axis_torque(axis))
        self._target_row(panel, 3, "速度 r/s", vars_for_axis["velocity"], lambda axis=axis: self._send_axis_velocity(axis))
        self._target_row(panel, 4, "位置 r", vars_for_axis["position"], lambda axis=axis: self._send_axis_position(axis))
        ttk.Button(panel, text="设为零点", style="Ghost.TButton", command=lambda axis=axis: self._call_axis(axis, "set_home")).grid(
            row=5, column=0, columnspan=2, sticky=tk.EW, pady=(8, 0)
        )
        ttk.Button(panel, text="同步", style="Accent.TButton", command=lambda axis=axis: self._call_axis(axis, "sync")).grid(
            row=5, column=2, sticky=tk.EW, padx=(6, 0), pady=(8, 0)
        )
        panel.columnconfigure(1, weight=1)

    def _target_row(self, parent: ttk.Frame, row: int, label: str, var: tk.DoubleVar, command) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, pady=(8, 2))
        frame = ttk.Frame(parent)
        frame.grid(row=row, column=1, columnspan=2, sticky=tk.EW, padx=(8, 0), pady=(8, 2))
        ttk.Entry(frame, textvariable=var, width=10).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(frame, text="发送", style="Accent.TButton", command=command).pack(side=tk.LEFT, padx=(6, 0))

    def _build_config_tab(self) -> None:
        self.config_tab.columnconfigure(0, weight=1)
        self.config_tab.rowconfigure(1, weight=1)

        top = ttk.Frame(self.config_tab)
        top.pack(fill=tk.X)
        ttk.Button(top, text="读取全部", style="Accent.TButton", command=self._read_all_config).pack(side=tk.LEFT)
        ttk.Button(top, text="写入选中项", style="Accent.TButton", command=self._write_selected_config).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="保存到 Flash", style="Ghost.TButton", command=lambda: self._call_client("save_all_config")).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="恢复默认", style="Danger.TButton", command=self._reset_defaults).pack(side=tk.LEFT, padx=(8, 0))

        body = ttk.PanedWindow(self.config_tab, orient=tk.HORIZONTAL)
        body.pack(fill=tk.BOTH, expand=True, pady=(12, 0))

        left = ttk.Frame(body)
        right = ttk.Frame(body)
        body.add(left, weight=3)
        body.add(right, weight=2)

        list_box = ttk.LabelFrame(left, text="参数列表", padding=10)
        list_box.pack(fill=tk.BOTH, expand=True)
        columns = ("idx", "name", "value", "unit", "note")
        self.config_tree = ttk.Treeview(list_box, columns=columns, show="headings", selectmode="browse")
        self.config_tree.heading("idx", text="序号")
        self.config_tree.heading("name", text="参数")
        self.config_tree.heading("value", text="值")
        self.config_tree.heading("unit", text="单位")
        self.config_tree.heading("note", text="说明")
        self.config_tree.column("idx", width=56, anchor=tk.CENTER)
        self.config_tree.column("name", width=220)
        self.config_tree.column("value", width=140)
        self.config_tree.column("unit", width=90)
        self.config_tree.column("note", width=280)
        self.config_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.config_tree.bind("<<TreeviewSelect>>", lambda event: self._load_selected_config_to_editor())
        self.config_tree.tag_configure("odd", background=PANEL_ALT_BG)
        self.config_tree.tag_configure("even", background=PANEL_BG)

        scrollbar = ttk.Scrollbar(list_box, orient=tk.VERTICAL, command=self.config_tree.yview)
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

        editor = ttk.LabelFrame(right, text="编辑选中项", padding=12)
        editor.pack(fill=tk.BOTH, expand=True)
        self.selected_config_var = tk.StringVar(value="-")
        self.selected_unit_var = tk.StringVar(value="-")
        self.selected_note_var = tk.StringVar(value="请在左侧选择一个参数。")
        self.selected_value_var = tk.StringVar(value="")

        ttk.Label(editor, text="当前参数", style="Section.TLabel").grid(row=0, column=0, sticky=tk.W)
        ttk.Label(editor, textvariable=self.selected_config_var, style="Info.TLabel").grid(row=1, column=0, sticky=tk.W, pady=(2, 10))

        ttk.Label(editor, text="单位", style="Section.TLabel").grid(row=2, column=0, sticky=tk.W)
        ttk.Label(editor, textvariable=self.selected_unit_var, style="Info.TLabel").grid(row=3, column=0, sticky=tk.W, pady=(2, 10))

        ttk.Label(editor, text="说明", style="Section.TLabel").grid(row=4, column=0, sticky=tk.W)
        ttk.Label(editor, textvariable=self.selected_note_var, style="Info.TLabel", wraplength=260, justify=tk.LEFT).grid(
            row=5, column=0, sticky=tk.W, pady=(2, 14)
        )

        ttk.Label(editor, text="新值", style="Section.TLabel").grid(row=6, column=0, sticky=tk.W)
        value_row = ttk.Frame(editor)
        value_row.grid(row=7, column=0, sticky=tk.EW, pady=(4, 0))
        value_row.columnconfigure(0, weight=1)
        ttk.Entry(value_row, textvariable=self.selected_value_var).grid(row=0, column=0, sticky=tk.EW)
        ttk.Button(value_row, text="写入", style="Accent.TButton", command=self._write_selected_config).grid(row=0, column=1, padx=(8, 0))
        editor.columnconfigure(0, weight=1)

    def _build_calib_tab(self) -> None:
        box = ttk.LabelFrame(self.calib_tab, text="校准与齿槽补偿", padding=12)
        box.pack(fill=tk.X)
        ttk.Button(box, text="开始校准", style="Accent.TButton", command=lambda: self._call_client("start_calibration")).pack(side=tk.LEFT)
        ttk.Button(box, text="中止校准", style="Danger.TButton", command=lambda: self._call_client("abort_calibration")).pack(
            side=tk.LEFT, padx=(8, 0)
        )
        ttk.Button(box, text="开始齿槽补偿", style="Accent.TButton", command=lambda: self._call_client("start_anticogging")).pack(
            side=tk.LEFT, padx=(20, 0)
        )
        ttk.Button(box, text="中止齿槽补偿", style="Danger.TButton", command=lambda: self._call_client("abort_anticogging")).pack(
            side=tk.LEFT, padx=(8, 0)
        )

        self.calib_progress = ttk.Progressbar(self.calib_tab, maximum=128)
        self.calib_progress.pack(fill=tk.X, pady=(16, 4))
        self.anticogging_progress = ttk.Progressbar(self.calib_tab, maximum=5000)
        self.anticogging_progress.pack(fill=tk.X, pady=(4, 10))
        self.calib_status_var = tk.StringVar(value="等待固件回报")
        ttk.Label(self.calib_tab, textvariable=self.calib_status_var, style="Info.TLabel").pack(anchor=tk.W)

    def _build_dfu_tab(self) -> None:
        box = ttk.LabelFrame(self.dfu_tab, text="固件升级", padding=12)
        box.pack(fill=tk.X)
        self.fw_path_var = tk.StringVar(value="")
        ttk.Entry(box, textvariable=self.fw_path_var).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(box, text="浏览", style="Ghost.TButton", command=self._browse_fw).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(box, text="开始升级", style="Accent.TButton", command=self._start_dfu).pack(side=tk.LEFT, padx=(8, 0))
        self.dfu_progress = ttk.Progressbar(self.dfu_tab, maximum=100)
        self.dfu_progress.pack(fill=tk.X, pady=(16, 4))
        self.dfu_status_var = tk.StringVar(value="升级空闲")
        ttk.Label(self.dfu_tab, textvariable=self.dfu_status_var, style="Info.TLabel").pack(anchor=tk.W)

    def _build_log_tab(self) -> None:
        toolbar = ttk.Frame(self.log_tab)
        toolbar.pack(fill=tk.X, pady=(0, 10))
        ttk.Button(toolbar, text="清空日志", style="Ghost.TButton", command=self._clear_log).pack(side=tk.LEFT)

        log_frame = ttk.LabelFrame(self.log_tab, text="运行日志", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True)

        self.log_text = tk.Text(
            log_frame,
            height=16,
            wrap=tk.WORD,
            bg=LOG_BG,
            fg=LOG_FG,
            insertbackground=LOG_FG,
            selectbackground="#1d4ed8",
            selectforeground="#ffffff",
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
            font=(MONO_FAMILY, 9),
        )
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.configure(yscrollcommand=scrollbar.set)

    def _set_connection_badge(self, text: str, tone: str = "neutral") -> None:
        self.connection_state_var.set(text)
        if not hasattr(self, "connection_badge"):
            return
        palette = {
            "neutral": ("#e2e8f0", TEXT_FG),
            "busy": ("#fde68a", "#92400e"),
            "success": ("#bbf7d0", "#065f46"),
            "error": ("#fecaca", "#991b1b"),
        }
        bg, fg = palette.get(tone, palette["neutral"])
        self.connection_badge.configure(bg=bg, fg=fg)

    def _clear_log(self) -> None:
        if hasattr(self, "log_text"):
            self.log_text.delete("1.0", tk.END)

    def _browse_dll(self) -> None:
        path = filedialog.askopenfilename(title="选择 ControlCAN.dll", filetypes=[("DLL", "*.dll"), ("所有文件", "*.*")])
        if path:
            self.dll_var.set(path)

    def _browse_fw(self) -> None:
        path = filedialog.askopenfilename(title="选择固件文件", filetypes=[("固件文件", "*.bin *.hex"), ("所有文件", "*.*")])
        if path:
            self.fw_path_var.set(path)

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

        self.connect_btn.configure(state=tk.DISABLED)
        self._set_connection_badge("连接中...", "busy")
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
        self.poll_btn.configure(text="开始轮询")
        self.connect_btn.configure(state=tk.NORMAL)
        if self.bus is not None:
            self.bus.close()
        self.bus = None
        self.client = None
        self._set_connection_badge("未连接", "neutral")
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
            self._log(f"{method_name} 失败：{exc}")
            messagebox.showerror("命令失败", str(exc))

    def _call_axis(self, axis: str, method_name: str, *args) -> None:
        try:
            client = self._require_client()
            client.call_axis(axis, method_name, *args)
            self._log(f"{self._axis_short(axis)} {method_name} 成功")
        except Exception as exc:
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

            values_by_axis: dict[str, dict[int, float]] = {}
            for axis in axes:
                values_by_axis[axis] = {}
                for item in VALUE_ITEMS:
                    values_by_axis[axis][item.index] = client.get_axis_value(axis, item.index)

            for item in VALUE_ITEMS:
                if len(axes) == 1:
                    self.value_vars[item.index].set(f"{values_by_axis[axes[0]][item.index]:.5g}")
                else:
                    self.value_vars[item.index].set(
                        " / ".join(
                            f"{self._axis_short(axis)} {values_by_axis[axis][item.index]:.5g}"
                            for axis in axes
                        )
                    )
        except Exception as exc:
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

    def _read_all_config(self) -> None:
        try:
            client = self._require_client()
            for item in CONFIG_ITEMS:
                value = client.get_config(item)
                self.config_vars[item.index].set(f"{value:.8g}" if isinstance(value, float) else str(value))
                self.config_tree.set(str(item.index), "value", self.config_vars[item.index].get())
            self._log("参数读取完成")
        except Exception as exc:
            self._log(f"读取参数失败：{exc}")
            messagebox.showerror("读取参数失败", str(exc))

    def _load_selected_config_to_editor(self) -> None:
        selected = self.config_tree.selection()
        if not selected:
            return
        index = int(selected[0])
        item = next(item for item in CONFIG_ITEMS if item.index == index)
        self.selected_config_var.set(f"{item.index}. {item.label}")
        self.selected_unit_var.set(item.unit or "-")
        self.selected_note_var.set(item.note or "该参数暂无额外说明。")
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
                self.dfu_status_var.set(f"升级失败：{payload}")
                self._log(f"升级失败：{payload}")
                messagebox.showerror("升级失败", str(payload))
            elif kind == "connect_done":
                bus, node_id = payload
                self.bus = bus
                self.client = CtmClient(bus, node_id)
                self.client.on_event = self._handle_event
                self.connect_btn.configure(state=tk.NORMAL)
                self._set_connection_badge("已连接", "success")
                self._log("连接成功")
            elif kind == "connect_error":
                self.bus = None
                self.client = None
                self.connect_btn.configure(state=tk.NORMAL)
                self._set_connection_badge("连接失败", "error")
                self._log(f"连接失败：{payload}")
                messagebox.showerror("连接失败", str(payload))
        self.after(100, self._drain_worker_queue)

    def _handle_event(self, event) -> None:
        if event.kind == "calibration":
            self.calib_progress["value"] = min(max(event.step - 10, 0), 128) if event.step >= 10 else event.step
            self.calib_status_var.set(f"校准回报：步骤={event.step} 数据={event.value!r}")
        elif event.kind == "anticogging":
            self.anticogging_progress["value"] = min(max(event.step, 0), 5000)
            self.calib_status_var.set(f"齿槽补偿：步骤={event.step} 值={event.value}")
        else:
            self._log(f"事件 {event.kind}：{event.step} {event.value}")

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

"""The window of Tisma Cloud on the screen of a server: the role of the machine (master or computing node), its
settings, starting and stopping the service, its log and, on the master, the administration. tkinter (part of
Python), so that the executable needs nothing else."""

from __future__ import annotations

import os
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, simpledialog, ttk

from .manager import JOB_CLASS_NAMES, MASTER_FIELDS, NODE_FIELDS, ROLE_NAMES, Manager

VIOLET = "#7A24C9"
STATUS_COLORS = {"running": "#1E9E52", "starting": "#D9A400", "stopped": "#9A9AA6"}


def _hide_own_console() -> None:
    """Windows: the window was opened with a double click; the console the executable opened is not needed."""
    if os.name != "nt" or not getattr(sys, "frozen", False):
        return
    import ctypes

    kernel32 = ctypes.windll.kernel32
    processes = (ctypes.c_uint * 4)()
    if kernel32.GetConsoleProcessList(processes, 4) == 1:  # only this process uses the console: it is its own
        kernel32.FreeConsole()


class App:
    def __init__(self, root: tk.Tk, manager: Manager) -> None:
        self.root = root
        self.m = manager
        self.fields: dict[str, tk.Variable] = {}
        root.title("Tisma Cloud")
        root.geometry("860x640")
        root.minsize(720, 520)

        style = ttk.Style(root)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("Title.TLabel", font=("TkDefaultFont", 15, "bold"), foreground=VIOLET)
        style.configure("Accent.TButton", foreground="white", background=VIOLET)
        style.map("Accent.TButton", background=[("active", "#8E34DF"), ("disabled", "#C9C9D2")])

        top = ttk.Frame(root, padding=(16, 12))
        top.pack(fill="x")
        ttk.Label(top, text="Tisma Cloud", style="Title.TLabel").pack(side="left")
        home = str(self.m.home)
        ttk.Label(top, text=home if len(home) <= 60 else "…" + home[-59:], foreground="#6B6B76").pack(side="right")

        # Role of the machine.
        role_box = ttk.LabelFrame(root, text="Rol de esta máquina", padding=(12, 8))
        role_box.pack(fill="x", padx=16)
        self.role = tk.StringVar(value=self.m.role())
        for role in ("master", "node"):
            ttk.Radiobutton(role_box, text=ROLE_NAMES[role].capitalize(), value=role, variable=self.role,
                            command=self.on_role).pack(side="left", padx=(0, 24))

        # Service.
        service = ttk.Frame(root, padding=(16, 10))
        service.pack(fill="x")
        self.dot = tk.Canvas(service, width=14, height=14, highlightthickness=0)
        self.dot.pack(side="left")
        self.status_text = ttk.Label(service, text="")
        self.status_text.pack(side="left", padx=(6, 0))
        self.autostart = tk.BooleanVar(value=False)
        ttk.Checkbutton(service, text="Iniciar con la máquina", variable=self.autostart,
                        command=self.on_autostart).pack(side="right")
        # Windows: the port of the master in the firewall (Windows Server closes it by default).
        self.firewall = tk.BooleanVar(value=False)
        self.firewall_box = ttk.Checkbutton(service, text="Abrir el puerto en el firewall", variable=self.firewall,
                                            command=self.on_firewall)
        if os.name == "nt":
            self.firewall_box.pack(side="right", padx=(0, 12))
        self.buttons = {}
        for key, label in (("restart", "Reiniciar"), ("stop", "Detener"), ("start", "Iniciar")):
            b = ttk.Button(service, text=label, command=lambda k=key: self.service(k),
                           style="Accent.TButton" if key == "start" else "TButton")
            b.pack(side="right", padx=4)
            self.buttons[key] = b

        self.tabs = ttk.Notebook(root)
        self.tabs.pack(fill="both", expand=True, padx=16, pady=(0, 16))
        self.settings_tab = ttk.Frame(self.tabs, padding=12)
        self.admin_tab = ttk.Frame(self.tabs, padding=12)
        self.log_tab = ttk.Frame(self.tabs, padding=12)
        self.check_tab = ttk.Frame(self.tabs, padding=12)
        self.tabs.add(self.settings_tab, text="Configuración")
        self.tabs.add(self.admin_tab, text="Administración")
        self.tabs.add(self.check_tab, text="Diagnóstico")
        self.tabs.add(self.log_tab, text="Registro")

        self.log = scrolledtext.ScrolledText(self.log_tab, height=10, font=("TkFixedFont", 9), state="disabled")
        self.log.pack(fill="both", expand=True)
        self.build_admin()
        self.build_settings()
        self.build_checks()
        threading.Thread(target=self.load_autostart, daemon=True).start()
        self.refresh()
        self.load_nodes()

    # ---- settings
    def build_settings(self) -> None:
        for child in self.settings_tab.winfo_children():
            child.destroy()
        self.fields.clear()
        role = self.m.role()
        if not role:
            ttk.Label(self.settings_tab, text="Elegí arriba si esta máquina es el maestro (el servidor central, que "
                      "recibe a los usuarios) o un nodo de cómputo (que lamina y calcula para el maestro).",
                      wraplength=760).pack(anchor="w")
            return
        form = ttk.Frame(self.settings_tab)
        form.pack(fill="x")
        form.columnconfigure(1, weight=1)
        rows: list[tuple[str, str, object, bool]] = []
        if role == "master":
            env = self.m.read_env()
            rows.append(("port", "Puerto", self.m.port(), False))
            rows += [(key, label, env.get(key, ""), secret) for key, label, secret in MASTER_FIELDS]
        else:
            node = self.m.read_node()
            rows += [(key, label, node.get(key, ""), secret) for key, label, _kind, secret in NODE_FIELDS]
        for r, (key, label, value, secret) in enumerate(rows):
            ttk.Label(form, text=label).grid(row=r, column=0, sticky="w", pady=3, padx=(0, 12))
            if isinstance(value, bool):
                var: tk.Variable = tk.BooleanVar(value=value)
                ttk.Checkbutton(form, variable=var).grid(row=r, column=1, sticky="w")
            else:
                var = tk.StringVar(value=str(value))
                ttk.Entry(form, textvariable=var, show="•" if secret else "").grid(row=r, column=1, sticky="ew")
                if key in ("tisma_slicer", "tisma_fea"):
                    ttk.Button(form, text="Buscar…", command=lambda v=var: self.browse(v)).grid(row=r, column=2,
                                                                                               padx=(6, 0))
            self.fields[key] = var
        bottom = ttk.Frame(self.settings_tab)
        bottom.pack(fill="x", pady=(12, 0))
        ttk.Button(bottom, text="Guardar", style="Accent.TButton", command=self.save).pack(side="right")
        if role == "node":
            ttk.Button(bottom, text="Buscar Tisma Slicer", command=self.find_programs).pack(side="right", padx=6)
        ttk.Label(bottom, text="Los cambios se usan al reiniciar el servicio.", foreground="#6B6B76").pack(side="left")

    def find_programs(self) -> None:
        """The programs of Tisma Slicer installed on this machine, for the node."""
        found = self.m.detect_programs(replace=True)
        if found:
            self.build_settings()
            messagebox.showinfo("Tisma Cloud", "Encontrados:\n" + "\n".join(found.values()), parent=self.root)
        else:
            messagebox.showwarning("Tisma Cloud", "No se encontró Tisma Slicer en esta máquina: instalalo, o indicá "
                                   "los programas con «Buscar…».", parent=self.root)
        self.load_checks()

    def browse(self, var: tk.StringVar) -> None:
        path = filedialog.askopenfilename(parent=self.root)
        if path:
            var.set(path)

    def save(self) -> None:
        try:
            if self.m.role() == "master":
                self.m.set_port(int(self.fields["port"].get()))
                self.m.write_env({k: str(v.get()) for k, v in self.fields.items() if k != "port"})
            else:
                self.m.write_node({k: v.get() for k, v in self.fields.items()})
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Tisma Cloud", f"No se pudo guardar: {e}", parent=self.root)
            return
        if self.m.status().running and messagebox.askyesno("Tisma Cloud", "Guardado. ¿Reiniciar el servicio ahora?",
                                                           parent=self.root):
            self.service("restart")

    # ---- diagnosis: what the machine needs to serve
    def build_checks(self) -> None:
        ttk.Label(self.check_tab, text="Lo que esta máquina necesita para dar servicio. Elegí un punto marcado con ✖ "
                  "y tocá «Arreglar» (algunos arreglos piden abrir Tisma Cloud como administrador).",
                  wraplength=780).pack(anchor="w", pady=(0, 8))
        self.check_view = ttk.Treeview(self.check_tab, columns=("detail",), height=9, selectmode="browse")
        self.check_view.heading("#0", text="Punto")
        self.check_view.heading("detail", text="Estado")
        self.check_view.column("#0", width=230, stretch=False)
        self.check_view.pack(fill="both", expand=True)
        self.check_view.tag_configure("ok", foreground="#1E9E52")
        self.check_view.tag_configure("bad", foreground="#C62828")
        self.check_view.tag_configure("warn", foreground="#B07D00")
        self.check_view.bind("<Double-1>", lambda _e: self.fix_selected())
        # The whole text of the chosen point (the list cuts long ones).
        self.check_detail = ttk.Label(self.check_tab, text="", wraplength=780, foreground="#4A4A55")
        self.check_detail.pack(anchor="w", pady=(6, 0))
        self.check_view.bind("<<TreeviewSelect>>", lambda _e: self.show_check_detail())
        bar = ttk.Frame(self.check_tab)
        bar.pack(fill="x", pady=(8, 0))
        self.fix_button = ttk.Button(bar, text="Arreglar", style="Accent.TButton", command=self.fix_selected)
        self.fix_button.pack(side="left")
        ttk.Button(bar, text="Arreglar todo", command=self.fix_all).pack(side="left", padx=6)
        ttk.Button(bar, text="Volver a comprobar", command=self.load_checks).pack(side="right")
        self.checks: list = []
        self.load_checks()

    def load_checks(self) -> None:
        """The checks run apart (some ask the network or PowerShell), then the list is filled."""
        self.check_view.delete(*self.check_view.get_children())
        self.check_view.insert("", "end", text="Comprobando…", values=("",))

        def work() -> None:
            try:
                checks = self.m.checks()
            except Exception as e:  # noqa: BLE001
                from .manager import Check

                checks = [Check("Diagnóstico", False, str(e))]
            self.root.after(0, lambda: self.show_checks(checks))

        threading.Thread(target=work, daemon=True).start()

    def show_checks(self, checks: list) -> None:
        self.checks = checks
        self.check_view.delete(*self.check_view.get_children())
        self.check_detail.config(text="")
        for i, c in enumerate(checks):
            mark, tag = {True: ("✔", "ok"), False: ("✖", "bad"), None: ("!", "warn")}[c.ok]
            fix = "  (se puede arreglar)" if c.ok is False and c.fix else ""
            self.check_view.insert("", "end", iid=str(i), text=f"{mark} {c.name}", values=(c.detail + fix,), tags=(tag,))

    def show_check_detail(self) -> None:
        sel = self.check_view.selection()
        if sel and sel[0].isdigit() and int(sel[0]) < len(self.checks):
            c = self.checks[int(sel[0])]
            self.check_detail.config(text=f"{c.name}: {c.detail}")

    def fix_selected(self) -> None:
        sel = self.check_view.selection()
        check = self.checks[int(sel[0])] if sel and sel[0].isdigit() and int(sel[0]) < len(self.checks) else None
        if check is None or not check.fix:
            messagebox.showinfo("Tisma Cloud", "Elegí un punto que se pueda arreglar.", parent=self.root)
            return
        self.run_fixes([check])

    def fix_all(self) -> None:
        self.run_fixes([c for c in self.checks if c.ok is False and c.fix])

    def run_fixes(self, checks: list) -> None:
        def work() -> None:
            done, errors = [], []
            for c in checks:
                try:
                    done.append(self.m.fix(c.fix))
                except Exception as e:  # noqa: BLE001
                    errors.append(f"{c.name}: {e}")
            self.root.after(0, lambda: self.after_fixes(done, errors))

        threading.Thread(target=work, daemon=True).start()

    def after_fixes(self, done: list[str], errors: list[str]) -> None:
        if errors:
            hint = "\n\nEn Windows, abrí Tisma Cloud como administrador (clic derecho > Ejecutar como administrador)."
            messagebox.showerror("Tisma Cloud", "\n".join(errors) + hint, parent=self.root)
        elif done:
            messagebox.showinfo("Tisma Cloud", "\n".join(done), parent=self.root)
        self.build_settings()
        threading.Thread(target=self.load_autostart, daemon=True).start()
        self.load_checks()
        self.refresh(reschedule=False)

    # ---- administration of the master
    def build_admin(self) -> None:
        # The nodes: what each one does.
        nodes_box = ttk.LabelFrame(self.admin_tab, text="Nodos: qué hace cada uno", padding=8)
        nodes_box.pack(fill="x", pady=(0, 10))
        columns = ("state", "does")
        self.nodes_view = ttk.Treeview(nodes_box, columns=columns, height=4, selectmode="browse")
        self.nodes_view.heading("#0", text="Nodo")
        self.nodes_view.heading("state", text="Estado")
        self.nodes_view.heading("does", text="Qué hace, en orden de prioridad")
        self.nodes_view.column("#0", width=120, stretch=False)
        self.nodes_view.column("state", width=150, stretch=False)
        self.nodes_view.pack(fill="x")
        self.nodes_view.bind("<Double-1>", lambda _e: self.edit_node())
        node_bar = ttk.Frame(nodes_box)
        node_bar.pack(fill="x", pady=(6, 0))
        ttk.Button(node_bar, text="Qué hace…", command=self.edit_node).pack(side="left")
        ttk.Button(node_bar, text="Habilitar / deshabilitar", command=self.toggle_node).pack(side="left", padx=6)
        ttk.Button(node_bar, text="Agregar nodo…", command=self.add_node).pack(side="left")
        ttk.Button(node_bar, text="Actualizar", command=self.load_nodes).pack(side="right")
        self.node_list: list[dict] = []

        bar = ttk.Frame(self.admin_tab)
        bar.pack(fill="x")
        ttk.Button(bar, text="Panel de control", style="Accent.TButton", command=self.open_panel).pack(side="left")
        for label, args in (("Nodos", ["node-list"]), ("Usuarios", ["user-list"]), ("Cola", ["queue"]),
                            ("Planes", ["plan-list"])):
            ttk.Button(bar, text=label, command=lambda a=args: self.admin(a)).pack(side="left", padx=(6, 0))
        ttk.Button(bar, text="Cuenta interna…", command=self.make_internal).pack(side="left", padx=(12, 0))
        line = ttk.Frame(self.admin_tab)
        line.pack(fill="x", pady=(10, 6))
        ttk.Label(line, text="Comando:").pack(side="left")
        self.admin_line = tk.StringVar()
        entry = ttk.Entry(line, textvariable=self.admin_line)
        entry.pack(side="left", fill="x", expand=True, padx=6)
        entry.bind("<Return>", lambda _e: self.admin(self.admin_line.get().split()))
        ttk.Button(line, text="Ejecutar", command=lambda: self.admin(self.admin_line.get().split())).pack(side="left")
        self.admin_out = scrolledtext.ScrolledText(self.admin_tab, height=10, font=("TkFixedFont", 9))
        self.admin_out.pack(fill="both", expand=True)

    def admin(self, args: list[str]) -> None:
        from .cli import admin_output

        if self.m.role() != "master":
            messagebox.showinfo("Tisma Cloud", "La administración es del maestro.", parent=self.root)
            return
        if not args:
            return
        _code, output = admin_output(self.m, args)
        self.admin_out.delete("1.0", "end")
        self.admin_out.insert("end", f"> {' '.join(args)}\n{output}")

    def add_node(self) -> None:
        name = simpledialog.askstring("Agregar nodo", "Nombre del nodo (por ejemplo i7-1):", parent=self.root)
        if not name:
            return
        priorities = simpledialog.askstring("Agregar nodo", "Qué hace, en orden (slice, slice_large, fea, "
                                            "fea_optimize):", initialvalue="slice,slice_large,fea", parent=self.root)
        if priorities:
            self.admin(["node-add", name, "--priorities", priorities])
            self.load_nodes()

    def load_nodes(self) -> None:
        self.nodes_view.delete(*self.nodes_view.get_children())
        if self.m.role() != "master":
            return
        try:
            self.node_list = self.m.nodes()
        except Exception as e:  # noqa: BLE001
            self.node_list = []
            self.admin_out.delete("1.0", "end")
            self.admin_out.insert("end", f"No se pudieron leer los nodos: {e}")
        for n in self.node_list:
            state = ("en línea" if n["online"] else "sin señal") + ("" if n["enabled"] else " · deshabilitado")
            does = " → ".join(JOB_CLASS_NAMES[p] for p in n["priorities"])
            self.nodes_view.insert("", "end", iid=n["name"], text=n["name"], values=(state, does))

    def selected_node(self) -> dict | None:
        sel = self.nodes_view.selection()
        node = next((n for n in self.node_list if sel and n["name"] == sel[0]), None)
        if node is None:
            messagebox.showinfo("Tisma Cloud", "Elegí un nodo de la lista.", parent=self.root)
        return node

    def toggle_node(self) -> None:
        node = self.selected_node()
        if node:
            self.m.set_node(node["name"], enabled=not node["enabled"])
            self.load_nodes()

    def edit_node(self) -> None:
        """What a node does: the job classes it takes, in order (the first ones first)."""
        node = self.selected_node()
        if node is None:
            return
        dialog = tk.Toplevel(self.root)
        dialog.title(f"Qué hace {node['name']}")
        dialog.transient(self.root)
        ttk.Label(dialog, text="Marcá lo que hace este nodo y ordenalo: toma primero los trabajos de arriba.",
                  padding=(12, 10)).pack(anchor="w")
        order = node["priorities"] + [c for c in JOB_CLASS_NAMES if c not in node["priorities"]]
        checked = {c: tk.BooleanVar(value=c in node["priorities"]) for c in JOB_CLASS_NAMES}
        rows = ttk.Frame(dialog, padding=(12, 0))
        rows.pack(fill="x")

        def draw() -> None:
            for child in rows.winfo_children():
                child.destroy()
            for i, c in enumerate(order):
                ttk.Checkbutton(rows, text=JOB_CLASS_NAMES[c], variable=checked[c]).grid(row=i, column=0, sticky="w",
                                                                                         pady=2)
                ttk.Button(rows, text="↑", width=3, command=lambda i=i: move(i, -1)).grid(row=i, column=1, padx=2)
                ttk.Button(rows, text="↓", width=3, command=lambda i=i: move(i, 1)).grid(row=i, column=2)

        def move(i: int, step: int) -> None:
            j = i + step
            if 0 <= j < len(order):
                order[i], order[j] = order[j], order[i]
                draw()

        def save() -> None:
            chosen = [c for c in order if checked[c].get()]
            try:
                self.m.set_node(node["name"], priorities=chosen)
            except Exception as e:  # noqa: BLE001
                messagebox.showerror("Tisma Cloud", str(e), parent=dialog)
                return
            dialog.destroy()
            self.load_nodes()

        draw()
        buttons = ttk.Frame(dialog, padding=12)
        buttons.pack(fill="x")
        ttk.Button(buttons, text="Guardar", style="Accent.TButton", command=save).pack(side="right")
        ttk.Button(buttons, text="Cancelar", command=dialog.destroy).pack(side="right", padx=6)

    def make_internal(self) -> None:
        email = simpledialog.askstring("Cuenta interna", "Correo de la cuenta (lamina sin límite y sin cobro):",
                                       parent=self.root)
        if email:
            self.admin(["user-set", email, "--unlimited"])

    def open_panel(self) -> None:
        """The panel of the master (servers and what each does, queue, jobs, users and internal accounts) in its own
        window, signed in as the administrator (tisma-cloud panel)."""
        import subprocess

        from .manager import run_command

        if not self.m.health():
            messagebox.showinfo("Tisma Cloud", "Iniciá el maestro para abrir el panel.", parent=self.root)
            return
        if os.name == "nt":
            from .cli import PANEL_WITHOUT_WEBVIEW2
            from .system import webview2_version

            if webview2_version() is None:
                messagebox.showwarning("Tisma Cloud", PANEL_WITHOUT_WEBVIEW2.format(port=self.m.port()),
                                       parent=self.root)
                return
        try:
            if not self.m.has_admin():
                email = simpledialog.askstring("Primer administrador", "Correo del administrador:", parent=self.root)
                if not email:
                    return
                password = simpledialog.askstring("Primer administrador", "Contraseña (8 caracteres o más):",
                                                  show="•", parent=self.root)
                if not password:
                    return
                self.m.create_admin(email, password)
            # The window of the panel runs in its own process (the engine of the window needs its main thread).
            subprocess.Popen(run_command()[:-1] + ["panel"], cwd=str(self.m.home))
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Tisma Cloud", f"No se pudo abrir el panel: {e}", parent=self.root)

    # ---- role and service
    def on_role(self) -> None:
        role = self.role.get()
        if role == self.m.role():
            return
        try:
            self.m.set_role(role)
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Tisma Cloud", str(e), parent=self.root)
            self.role.set(self.m.role())
            return
        self.build_settings()
        # One refresh: the periodic one is already scheduled.
        self.refresh(reschedule=False)
        self.load_nodes()
        self.load_checks()

    def service(self, action: str) -> None:
        for b in self.buttons.values():
            b.state(["disabled"])
        self.status_text.config(text={"start": "Iniciando…", "stop": "Deteniendo…", "restart": "Reiniciando…"}[action])

        def work() -> None:
            error = None
            try:
                getattr(self.m, action)()
            except Exception as e:  # noqa: BLE001
                error = str(e)
            self.root.after(0, lambda: self.after_service(error))

        threading.Thread(target=work, daemon=True).start()

    def after_service(self, error: str | None) -> None:
        if error:
            messagebox.showerror("Tisma Cloud", error, parent=self.root)
        self.refresh(reschedule=False)
        self.load_checks()

    def load_autostart(self) -> None:
        try:
            enabled = self.m.autostart_enabled()
        except Exception:  # noqa: BLE001
            enabled = False
        try:
            firewall = bool(self.m.firewall_open())
        except Exception:  # noqa: BLE001
            firewall = False
        self.root.after(0, lambda: (self.autostart.set(enabled), self.firewall.set(firewall)))

    def on_firewall(self) -> None:
        try:
            self.m.set_firewall(self.firewall.get())
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Tisma Cloud", f"{e}\n\nEn Windows hay que abrir Tisma Cloud como administrador.",
                                 parent=self.root)
            self.firewall.set(not self.firewall.get())
        self.load_checks()

    def on_autostart(self) -> None:
        try:
            self.m.set_autostart(self.autostart.get())
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Tisma Cloud", f"{e}\n\nEn Windows hay que abrir Tisma Cloud como administrador.",
                                 parent=self.root)
            self.autostart.set(not self.autostart.get())

    def refresh(self, reschedule: bool = True) -> None:
        st = self.m.status()
        state = "running" if st.running and (st.role != "master" or st.healthy) else \
                "starting" if st.running else "stopped"
        self.dot.delete("all")
        self.dot.create_oval(2, 2, 12, 12, fill=STATUS_COLORS[state], outline="")
        self.status_text.config(text=st.detail)
        has_role = bool(st.role)
        self.buttons["start"].state(["!disabled"] if has_role and not st.running else ["disabled"])
        self.buttons["stop"].state(["!disabled"] if st.running else ["disabled"])
        self.buttons["restart"].state(["!disabled"] if st.running else ["disabled"])
        self.tabs.tab(self.admin_tab, state="normal" if st.role == "master" else "disabled")
        self.firewall_box.state(["!disabled"] if st.role == "master" else ["disabled"])
        lines = self.m.logs(300)
        self.log.config(state="normal")
        self.log.delete("1.0", "end")
        self.log.insert("end", "\n".join(lines) or "(sin registro todavía)")
        self.log.see("end")
        self.log.config(state="disabled")
        if reschedule:
            self.root.after(3000, self.refresh)


def main(manager: Manager | None = None) -> int:
    _hide_own_console()
    manager = manager or Manager()
    os.chdir(manager.home)
    root = tk.Tk()
    App(root, manager)
    root.mainloop()
    return 0

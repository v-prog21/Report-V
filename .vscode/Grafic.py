import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import customtkinter as ctk
import sqlite3
import os
import json
import hashlib
import threading
from datetime import datetime, date, timedelta
import ctypes
from ctypes import wintypes
import sys
import webbrowser
import requests
import re

import openpyxl
from openpyxl.styles import Font, Alignment, Border, Side, PatternFill
from openpyxl.utils import get_column_letter

from dialogs import TripDialog, DevicesPickerDialog, TripsListWindow
import utils

# Константы календаря (фиксированные размеры ячеек для одинаковой высоты строк
# и стабильной ширины при смене месяцев)
CELL_WIDTH = 12            # ширина заголовка дня недели (в символах)
CELL_PIXEL_WIDTH = 130     # минимальная ширина ячейки в пикселях
CELL_HEIGHT = 78           # фиксированная высота строки ячейки в пикселях
MONTH_LABEL_WIDTH = 16     # фиксированная ширина надписи «Месяц Год» (в символах)

class MDBViewerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Просмотр базы данных приборов")
        self.root.geometry("1200x720")

        # Настройка светлой темы (Windows 11 style)
        ctk.set_appearance_mode("light")
        ctk.set_default_color_theme("blue")
        self._apply_light_theme()
        
        # Определяем базовую директорию
        if getattr(sys, 'frozen', False):
            self.base_dir = os.path.dirname(sys.executable)
        else:
            self.base_dir = os.path.dirname(os.path.abspath(__file__))
        
        self.config_file = os.path.join(self.base_dir, "config.json")
        self.mdb_path = ""
        self.db_path = os.path.join(self.base_dir, "devices.db")
        self.is_loading = False
        self.first_run = False
        
        # Словарь расшифровки факторов
        self.factors_dict = {
            1: "Химический (Х)",
            2: "Биологический (Б)",
            3: "Аэрозоли ПФД (П)",
            4: "Шум (Ш)",
            5: "Инфразвук (ИЗ)",
            6: "Ультразвук (УЗ)",
            7: "Вибрация общая (ВО)",
            8: "Вибрация локальная (ВЛ)",
            9: "ЭМП (ЭМП)",
            10: "Ионизирующие излучения (ИИ)",
            11: "Микроклимат (М)",
            12: "Освещение (О)",
            13: "Тяжесть (ТТ)",
            14: "Напряженность (НТ)",
            15: "Травмоопасность (Тр)",
            16: "СИЗ (СИЗ)",
            25: "Аэроионизация (Аи)",
            26: "Ультрафиолетовое излучение (УФ)",
            41: "Лазерное излучение (ЛИ)"
        }
        
        self.load_config()
        self.check_first_run()
        self.create_local_db()
        self.create_ui()
        
        if self.first_run:
            # Первый запуск - сразу запрашиваем файл
            self.root.after(100, self.choose_mdb_file)
        elif self.mdb_path and os.path.exists(self.mdb_path):
            # Повторный запуск - только обновляем индикаторы
            self.root.after(100, self.update_indicators_only)
        else:
            self.root.after(100, self.choose_mdb_file)
    
    def check_first_run(self):
        """Проверяем, существует ли локальная база данных"""
        self.first_run = not os.path.exists(self.db_path)

    def _apply_light_theme(self):
        """Применяет светлый Windows-11 стиль к нативным виджетам ttk."""
        self.root.configure(bg="#f5f6fa")
        self.style = ttk.Style(self.root)
        try:
            self.style.theme_use("clam")
        except Exception:
            pass

        self.style.configure(".", background="#f5f6fa", foreground="#1f2933",
                             fieldbackground="#ffffff", troughcolor="#e8ebf1",
                             bordercolor="#d3d9e3", lightcolor="#f5f6fa",
                             darkcolor="#c9d1dc", font=("Segoe UI", 10))
        self.style.configure("TFrame", background="#f5f6fa")
        self.style.configure("TLabel", background="#f5f6fa", foreground="#1f2933",
                             font=("Segoe UI", 10))
        self.style.configure("TNotebook", background="#f5f6fa", borderwidth=0)
        self.style.configure("TNotebook.Tab", background="#e8ebf1", foreground="#3b4351",
                             padding=[16, 10], font=("Segoe UI", 10))
        # Одинаковый отступ/высота для всех состояний вкладки, чтобы заголовки
        # не "прыгали" вниз при выборе (selected-вкладка не растёт по высоте).
        self.style.map("TNotebook.Tab",
                       background=[("selected", "#ffffff"), ("active", "#f0f3f8")],
                       foreground=[("selected", "#0b5fff"), ("active", "#0b5fff")],
                       padding=[("selected", [16, 10]), ("active", [16, 10]),
                                ("!selected", [16, 10])])
        self.style.configure("Treeview", background="#ffffff", fieldbackground="#ffffff",
                             foreground="#1f2933", rowheight=28,
                             font=("Segoe UI", 10), bordercolor="#d3d9e3")
        self.style.configure("Treeview.Heading", background="#f0f3f8", foreground="#3b4351",
                             font=("Segoe UI", 10, "bold"), relief="flat",
                             padding=[6, 6])
        self.style.map("Treeview.Heading",
                       background=[("active", "#e2e9f5")],
                       foreground=[("active", "#0b5fff")])
        self.style.map("Treeview",
                       background=[("selected", "#dbeafe")],
                       foreground=[("selected", "#0b3050")])
        self.style.configure("Vertical.TScrollbar", background="#c9d1dc",
                             troughcolor="#e8ebf1", bordercolor="#c9d1dc",
                             arrowcolor="#5b6472")
        self.style.configure("Horizontal.TScrollbar", background="#c9d1dc",
                             troughcolor="#e8ebf1", bordercolor="#c9d1dc",
                             arrowcolor="#5b6472")
        self.style.configure("TLabelframe", background="#f5f6fa", bordercolor="#d3d9e3")
        self.style.configure("TLabelframe.Label", background="#f5f6fa",
                             foreground="#3b4351", font=("Segoe UI", 10, "bold"))

    
    def _warn(self, parent, title, msg, kind="warning"):
        """Показывает окно предупреждения и возвращает фокус на исходный диалог.

        Параметр `kind` — тип messagebox: 'warning', 'error' или 'info'.
        После закрытия окна сообщения исходный диалог поднимается и получает
        фокус, чтобы он не сворачивался и не терял фокус.
        """
        func = {
            "warning": messagebox.showwarning,
            "error": messagebox.showerror,
            "info": messagebox.showinfo,
        }.get(kind, messagebox.showwarning)
        win = parent if parent is not None else self.root
        try:
            func(title, msg, parent=win)
        except Exception:
            func(title, msg)
        # Возврат фокуса на исходное окно
        for _ in range(3):
            try:
                win.lift()
                win.focus_force()
            except Exception:
                break
            self.root.update_idletasks()

    def load_config(self):
        try:
            if os.path.exists(self.config_file):
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    config = json.load(f)
                    self.mdb_path = config.get('mdb_path', '')
        except Exception:
            self.mdb_path = ""

    def save_config(self):
        try:
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump({'mdb_path': self.mdb_path}, f, ensure_ascii=False, indent=4)
        except:
            pass
    
    def create_local_db(self):
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS devices (
                id TEXT PRIMARY KEY,
                name TEXT,
                reg_num TEXT,
                factory_num TEXT,
                no_test INTEGER,
                fac_ids TEXT,
                si_err TEXT,
                dop_fld3 TEXT,
                active INTEGER DEFAULT 1,
                data_hash TEXT
            )
        ''')
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS device_tests (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                dev_id TEXT,
                org_reg TEXT,
                num_doc TEXT,
                begin_date TEXT,
                end_date TEXT,
                FOREIGN KEY (dev_id) REFERENCES devices (id)
            )
        ''')
        
        # Таблица для персонала
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS persons (
                id_person TEXT PRIMARY KEY,
                fio TEXT,
                dolg TEXT
            )
        ''')
        
        # Таблица для комплектов
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS kits (
                id_complect INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT,
                factors TEXT
            )
        ''')
        
        # Таблица для связи комплектов и приборов
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS kit_devices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                kit_id INTEGER,
                device_id TEXT,
                FOREIGN KEY (kit_id) REFERENCES kits (id_complect) ON DELETE CASCADE,
                FOREIGN KEY (device_id) REFERENCES devices (id)
            )
        ''')
        
        conn.commit()
        conn.close()
    
    def create_ui(self):
        # Создаем вкладки
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Вкладка "График выездов"
        self.tab_schedule = tk.Frame(self.notebook)
        self.notebook.add(self.tab_schedule, text="График выездов")
        
        # Вкладка "Комплекты приборов"
        self.tab_kits = tk.Frame(self.notebook)
        self.notebook.add(self.tab_kits, text="Комплекты приборов")
        
        # Вкладка "Приборы"
        self.tab_devices = tk.Frame(self.notebook)
        self.notebook.add(self.tab_devices, text="Приборы")
        
        # Вкладка "Персонал"
        self.tab_persons = tk.Frame(self.notebook)
        self.notebook.add(self.tab_persons, text="Персонал")
        
        # Вкладка "Настройки"
        self.tab_settings = tk.Frame(self.notebook)
        self.notebook.add(self.tab_settings, text="Настройки")
        
        # Создаем интерфейс для вкладки "Приборы"
        self.create_devices_tab()
        
        # Создаем интерфейс для вкладки "Персонал"
        self.create_persons_tab()
        
        # Создаем интерфейс для вкладки "Комплекты приборов"
        self.create_kits_tab()
        
        # Создаем интерфейс для вкладки "Настройки"
        self.create_settings_tab()
        
        # Создаем интерфейс для вкладки "График выездов"
        self.create_schedule_tab()
        
        # Отображаем текущие данные из локальной БД (если уже загружены ранее)
        self.display_data()
        self.display_persons()
        self.display_kits()

    # =====================================================================
    # График выездов
    # =====================================================================
    def get_schedule_db_path(self, year):
        """Возвращает путь к годовой БД выездов (создаёт папку arc при необходимости)"""
        arc_dir = os.path.join(self.base_dir, "arc")
        # Убеждаемся, что базовая директория существует (при первом запуске)
        if not os.path.exists(self.base_dir):
            os.makedirs(self.base_dir)
        if not os.path.exists(arc_dir):
            os.makedirs(arc_dir)
        return os.path.join(arc_dir, f"{year}.db")

    def ensure_schedule_tables(self, db_path):
        """Создаёт таблицы выездов в годовой БД, если их ещё нет"""
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS trips (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                begin_date TEXT,
                end_date TEXT,
                place TEXT,
                persons TEXT,
                kits TEXT,
                devices TEXT
            )
        ''')
        conn.commit()
        conn.close()

    def create_schedule_tab(self):
        """Создаёт интерфейс вкладки 'График выездов'"""
        # Инициализируем текущий месяц/год
        today = date.today()
        self.current_year = today.year
        self.current_month = today.month

        # Верхняя панель: навигация по месяцам (месяц/год по центру, стрелки с двух сторон)
        nav_frame = tk.Frame(self.tab_schedule)
        nav_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)
        nav_frame.grid_columnconfigure(0, weight=1)
        nav_frame.grid_columnconfigure(1, weight=0)
        nav_frame.grid_columnconfigure(2, weight=0)
        nav_frame.grid_columnconfigure(3, weight=1)

        self.btn_prev_month = ctk.CTkButton(nav_frame, text="◀", command=self.prev_month, width=40)
        self.btn_prev_month.grid(row=0, column=0, padx=2, sticky="e")

        self.lbl_month_year = tk.Label(
            nav_frame, text="", font=("Segoe UI", 16, "bold"),
            fg="#3b4351", bg="#f5f6fa",
            width=MONTH_LABEL_WIDTH, anchor="center",
            relief=tk.SOLID, bd=1,
            highlightbackground="#3b4351", highlightthickness=0,
        )
        self.lbl_month_year.grid(row=0, column=1, padx=20)

        self.btn_next_month = ctk.CTkButton(nav_frame, text="▶", command=self.next_month, width=40)
        self.btn_next_month.grid(row=0, column=2, padx=2, sticky="w")

        # Кнопка создания отчёта за выбранный месяц
        self.btn_create_report = ctk.CTkButton(
            nav_frame, text="Создать отчет", command=self.create_month_report, width=120,
            fg_color="transparent", border_width=1,
            text_color="#0b5fff", border_color="#0b5fff",
            hover_color="#e6f0ff")
        self.btn_create_report.grid(row=0, column=3, padx=(10, 2), sticky="e")

        # Область календаря
        self.calendar_frame = tk.Frame(self.tab_schedule)
        self.calendar_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.render_calendar()

    def prev_month(self):
        if self.current_month == 1:
            self.current_month = 12
            self.current_year -= 1
        else:
            self.current_month -= 1
        self.render_calendar()

    def next_month(self):
        if self.current_month == 12:
            self.current_month = 1
            self.current_year += 1
        else:
            self.current_month += 1
        self.render_calendar()

    def get_trips_for_month(self):
        """Возвращает словарь {дата (dd.mm.yyyy): [trips]} из годовой БД.

        Оптимизация: выбираем только выезды, пересекающиеся с текущим месяцем.
        Даты хранятся в формате дд.мм.гггг, который сортируется лексикографически.
        """
        db_path = self.get_schedule_db_path(self.current_year)
        self.ensure_schedule_tables(db_path)

        # Границы месяца в формате дд.мм.гггг
        first = date(self.current_year, self.current_month, 1)
        if self.current_month == 12:
            last = date(self.current_year + 1, 1, 1) - timedelta(days=1)
        else:
            last = date(self.current_year, self.current_month + 1, 1) - timedelta(days=1)
        month_start = first.strftime('%d.%m.%Y')
        month_end = last.strftime('%d.%m.%Y')

        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        # Выезд попадает в месяц, если его диапазон [begin, end] пересекает [month_start, month_end]
        cursor.execute("""
            SELECT id, begin_date, end_date, place, persons, kits, devices
            FROM trips
            WHERE (begin_date <= ?) AND (end_date >= ?)
        """, (month_end, month_start))
        rows = cursor.fetchall()
        conn.close()

        result = {}
        for row in rows:
            trip = {
                'id': row[0],
                'begin_date': row[1],
                'end_date': row[2],
                'place': row[3],
                'persons': row[4],
                'kits': row[5],
                'devices': row[6],
            }
            # Один выезд отображается на каждый день диапазона begin..end
            dates = self.expand_date_range(trip['begin_date'], trip['end_date'])
            for d in dates:
                result.setdefault(d, []).append(trip)
        return result

    def expand_date_range(self, begin_str, end_str):
        """Возвращает список дат dd.mm.yyyy между begin и end включительно"""
        return utils.expand_date_range(begin_str, end_str)

    def render_calendar(self):
        """Отрисовывает календарь на текущий месяц"""
        for widget in self.calendar_frame.winfo_children():
            widget.destroy()

        months_ru = ["Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
                     "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"]
        self.lbl_month_year.configure(text=f"{months_ru[self.current_month - 1]} {self.current_year}")

        # Получаем выезды месяца
        trips_by_date = self.get_trips_for_month()

        # Заголовки дней недели (одинаковая фиксированная ширина)
        weekdays = ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"]
        for col, wd in enumerate(weekdays):
            lbl = tk.Label(self.calendar_frame, text=wd, font=("Arial", 10, "bold"),
                           width=CELL_WIDTH, bd=1, relief=tk.RIDGE)
            lbl.grid(row=0, column=col, sticky="nsew", padx=1, pady=1)

        # Первый день месяца и его день недели (Пн=0)
        first_day = date(self.current_year, self.current_month, 1)
        start_col = first_day.weekday()  # Monday=0 .. Sunday=6

        # Количество дней в месяце
        if self.current_month == 12:
            days_in_month = (date(self.current_year + 1, 1, 1) - first_day).days
        else:
            days_in_month = (date(self.current_year, self.current_month + 1, 1) - first_day).days

        row = 1
        col = start_col
        for day in range(1, days_in_month + 1):
            day_date = date(self.current_year, self.current_month, day)
            date_str = day_date.strftime('%d.%m.%Y')

            cell = tk.Frame(self.calendar_frame, bd=1, relief=tk.RIDGE,
                            width=CELL_PIXEL_WIDTH, height=CELL_HEIGHT)
            cell.grid(row=row, column=col, sticky="nsew", padx=1, pady=1)
            cell.grid_propagate(False)
            cell.pack_propagate(False)

            # Верхняя строка ячейки: число + кнопка "+"
            top = tk.Frame(cell)
            top.pack(fill=tk.X)

            num_lbl = tk.Label(top, text=str(day), font=("Arial", 11, "bold"))
            num_lbl.pack(side=tk.LEFT, padx=2)

            btn_plus = tk.Button(top, text="+", width=2, height=1,
                                 font=("Arial", 11, "bold"),
                                 relief=tk.SOLID, bd=1,
                                 highlightbackground="black", highlightthickness=1,
                                 command=lambda d=date_str: self.open_trip_dialog(d))
            btn_plus.pack(side=tk.RIGHT, padx=2)

            # Индикаторы выездов (залитый квадрат "⬛" для лучшей видимости)
            trips = trips_by_date.get(date_str, [])
            # Проверка конфликтов приборов
            conflict_ids = self.find_trip_conflicts(trips)
            dot_row = tk.Frame(cell)
            dot_row.pack(fill=tk.X)
            dots = []
            for i, trip in enumerate(trips):
                color = "red" if trip['id'] in conflict_ids else "black"
                dot = tk.Label(dot_row, text="⬛", font=("Arial", 16), fg=color)
                dot.pack(side=tk.LEFT, padx=0)
                dots.append(dot)

            # Привязка событий (двойной ЛКМ / ПКМ) к ячейке и её содержимому,
            # кроме кнопки "+" (у неё своя команда)
            for w in (cell, top, num_lbl, dot_row) + tuple(dots):
                w.bind("<Double-Button-1>", lambda e, d=date_str: self.show_trips_window(d))
                w.bind("<Button-3>", lambda e, d=date_str: self.show_trips_window(d))

            col += 1
            if col > 6:
                col = 0
                row += 1

        # Фиксируем ширину колонок и высоту строк, чтобы календарь не "прыгал"
        # при смене месяцев (разное число дней / разная ширина).
        for c in range(7):
            self.calendar_frame.grid_columnconfigure(c, weight=1, minsize=CELL_PIXEL_WIDTH)
        # Ряды: заголовок + до 6 недель, все одинаковой высоты
        for r in range(7):
            self.calendar_frame.grid_rowconfigure(r, minsize=CELL_HEIGHT)

    def get_trip_device_list(self, trip):
        """Возвращает список ID приборов выезда (из комплектов + вручную)"""
        device_ids = set()
        # Приборы из комплектов
        kits_str = trip['kits'] or ""
        if kits_str:
            kit_ids = [k for k in kits_str.split(",") if k.strip()]
            if kit_ids:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                for kid in kit_ids:
                    try:
                        cursor.execute("SELECT device_id FROM kit_devices WHERE kit_id = ?", (int(kid),))
                        for r in cursor.fetchall():
                            device_ids.add(r[0])
                    except ValueError:
                        pass
                conn.close()
        # Приборы добавленные вручную
        devices_str = trip['devices'] or ""
        if devices_str:
            for d in devices_str.split(","):
                d = d.strip()
                if d:
                    device_ids.add(d)
        return device_ids

    def find_trip_conflicts(self, trips):
        """Возвращает множество id выездов, чьи приборы конфликтуют (повторяются) с другими выездами того же дня.

        Первый выезд (по порядку создания, id) остаётся чёрным, последующие с
        повторяющимся прибором помечаются красным.
        """
        if len(trips) < 2:
            return set()
        # Сортировка по id (порядок создания) — стабильный и однозначный критерий
        try:
            sorted_trips = sorted(trips, key=lambda t: t.get('id', 0))
        except TypeError:
            sorted_trips = list(trips)
        used_devices = {}
        conflict_ids = set()
        for trip in sorted_trips:
            devs = self.get_trip_device_list(trip)
            trip_conflicts = False
            for d in devs:
                if d in used_devices:
                    trip_conflicts = True
                    break
            if trip_conflicts:
                conflict_ids.add(trip['id'])
            for d in devs:
                used_devices[d] = True
        return conflict_ids

    def open_trip_dialog(self, default_date, trip_id=None):
        """Открывает окно создания/редактирования выезда"""
        TripDialog(self, self.root, default_date, trip_id)

    def get_device_name(self, device_id):
        """Возвращает наименование прибора по ID (или сам ID при отсутствии)"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            cursor.execute("SELECT name FROM devices WHERE id = ?", (device_id,))
            r = cursor.fetchone()
            conn.close()
            if r and r[0]:
                return r[0]
        except Exception:
            pass
        return device_id

    def show_trips_window(self, date_str):
        """Открывает окно со списком выездов на выбранную дату"""
        TripsListWindow(self, self.root, date_str)

    # ------------------------------------------------------------------ #
    #  Формирование Excel-отчёта «Календарь выездов» за выбранный месяц  #
    # ------------------------------------------------------------------ #
    def _report_resolve_persons(self, persons_str):
        """Возвращает строку коротких ФИО через запятую для отчёта."""
        if not persons_str:
            return ""
        ids = [p for p in persons_str.split(",") if p.strip()]
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        names = []
        for pid in ids:
            cursor.execute("SELECT fio FROM persons WHERE id_person = ?", (pid,))
            r = cursor.fetchone()
            if r and r[0]:
                names.append(self.short_fio(r[0]))
        conn.close()
        return ", ".join(names)

    def _report_resolve_kits(self, kits_str):
        """Возвращает строку наименований комплектов через запятую."""
        if not kits_str:
            return ""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        names = []
        for kid in [k for k in kits_str.split(",") if k.strip()]:
            try:
                cursor.execute("SELECT name FROM kits WHERE id_complect = ?", (int(kid),))
            except ValueError:
                continue
            r = cursor.fetchone()
            if r and r[0]:
                names.append(r[0])
        conn.close()
        return ", ".join(names)

    def _report_resolve_devices(self, devices_str):
        """Возвращает список строк «наименование (заводской номер)» доп. приборов."""
        if not devices_str:
            return []
        ids = [d.strip() for d in devices_str.split(",") if d.strip()]
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        result = []
        for did in ids:
            cursor.execute("SELECT name, factory_num FROM devices WHERE id = ?", (did,))
            r = cursor.fetchone()
            if not r:
                continue
            name = (r[0] or "").strip() or did
            fac = (r[1] or "").strip()
            if fac:
                result.append(f"{name} ({fac})")
            else:
                result.append(name)
        conn.close()
        return result

    def _report_trip_text(self, trip):
        """Формирует многострочный текст выезда для ячейки отчёта."""
        lines = []
        place = (trip.get('place') or "").strip()
        persons = self._report_resolve_persons(trip.get('persons'))
        day_persons = []
        for p in persons.split(", "):
            if p.strip():
                day_persons.append(p.strip())
        persons_short = ", ".join(day_persons)
        head = place
        if persons_short:
            head = f"{place} - {persons_short}" if place else persons_short
        if head:
            lines.append(head)
        kits = self._report_resolve_kits(trip.get('kits'))
        if kits:
            lines.append(f"Комплекты приборов: {kits}")
        devices = self._report_resolve_devices(trip.get('devices'))
        if devices:
            lines.append("Доп. приборы: " + ", ".join(devices))
        return "\n".join(lines)

    def create_month_report(self):
        """Генерирует Excel-отчёт «Календарь выездов» за текущий месяц календаря."""
        months_ru = ["Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
                     "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"]
        month_name = months_ru[self.current_month - 1]
        year = self.current_year
        title = f"{month_name} {year}"

        # Данные выездов по датам
        trips_by_date = self.get_trips_for_month()

        # Определяем сетку календаря (как в шаблоне)
        first_day = date(year, self.current_month, 1)
        start_col = first_day.weekday()  # Пн=0
        if self.current_month == 12:
            days_in_month = (date(year + 1, 1, 1) - first_day).days
        else:
            days_in_month = (date(year, self.current_month + 1, 1) - first_day).days

        # Создаём книгу
        wb = openpyxl.Workbook()
        ws = wb.active
        ws.title = month_name

        # Стили (соответствуют шаблону)
        thin = Side(style="thin", color="000000")
        border = Border(left=thin, right=thin, top=thin, bottom=thin)
        font_title = Font(name="Century Gothic", size=36, bold=True)
        font_week = Font(name="Century Gothic", size=12, bold=False)
        font_day = Font(name="Century Gothic", size=12, bold=False)
        font_trip = Font(name="Century Gothic", size=9, bold=False)
        yellow = PatternFill(start_color="FFFFFF00", end_color="FFFFFF00", fill_type="solid")

        # Заголовок (объединённая ячейка B2:I2, как в шаблоне)
        ws.merge_cells("B2:H2")
        title_cell = ws["B2"]
        title_cell.value = title + " "
        title_cell.font = font_title
        title_cell.alignment = Alignment(horizontal="left", vertical="center")
        for col in range(2, 9):
            ws.cell(row=2, column=col).border = border

        # Заголовки дней недели
        weekdays = ["Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье"]
        for i, wd in enumerate(weekdays):
            cell = ws.cell(row=3, column=2 + i, value=wd)
            cell.font = font_week
            cell.alignment = Alignment(horizontal="center", vertical="center")
            cell.border = border

        # Сетка недель: 6 недель, каждая = 1 строка чисел + 5 строк выездов
        ws.column_dimensions['A'].width = 1.66
        ws.column_dimensions['I'].width = 1.66
        ws.column_dimensions['B'].width = 40.78
        ws.column_dimensions['C'].width = 40.66
        ws.column_dimensions['D'].width = 40.66
        ws.column_dimensions['E'].width = 40.66
        ws.column_dimensions['F'].width = 40.66
        ws.column_dimensions['G'].width = 18.66
        ws.column_dimensions['H'].width = 40.66

        TRIPS_PER_DAY = 5
        WEEK_ROWS = 1 + TRIPS_PER_DAY  # 6

        # День -> номер строки его ячейки (для проставления чисел)
        day_row_map = {}
        # Каждая «ячейка дня» — это 5 строк выездов под числом
        day_trip_cells = {}  # (day) -> (start_trip_row)

        row_idx = 4  # первая строка чисел
        for week in range(6):
            for wd in range(7):
                col = 2 + wd
                day = week * 7 + wd - start_col + 1
                number_row = row_idx
                # число дня
                num_cell = ws.cell(row=number_row, column=col)
                num_cell.border = border
                num_cell.font = font_day
                num_cell.alignment = Alignment(horizontal="right", vertical="center")
                if 1 <= day <= days_in_month:
                    num_cell.value = day
                    date_str = date(year, self.current_month, day).strftime('%d.%m.%Y')
                    day_trip_cells[date_str] = number_row + 1
                # строки выездов под числом
                for sub in range(1, TRIPS_PER_DAY + 1):
                    tc = ws.cell(row=number_row + sub, column=col)
                    tc.border = border
                    tc.font = font_trip
                    tc.alignment = Alignment(horizontal="left", vertical="top", wrap_text=True)
            row_idx += WEEK_ROWS

        # Заполняем выезды
        for date_str, trips in trips_by_date.items():
            if date_str not in day_trip_cells:
                continue
            start_trip_row = day_trip_cells[date_str]
            # Упорядочиваем по id (порядок создания)
            sorted_trips = sorted(trips, key=lambda t: t.get('id', 0))
            conflict_ids = self.find_trip_conflicts(sorted_trips)
            # Определяем колонку для этой даты
            d = datetime.strptime(date_str, '%d.%m.%Y').date()
            col = 2 + d.weekday()
            for i, trip in enumerate(sorted_trips):
                if i >= TRIPS_PER_DAY:
                    break
                r = start_trip_row + i
                cell = ws.cell(row=r, column=col)
                cell.value = self._report_trip_text(trip)
                cell.alignment = Alignment(horizontal="left", vertical="top", wrap_text=True)
                cell.font = font_trip
                cell.border = border
                if trip.get('id') in conflict_ids:
                    cell.fill = yellow

        # Высоты строк: заголовок высокий, строки выездов — авто (не фиксируем,
        # Excel сам подстроит высоту под перенос текста). Число-строки — средняя.
        ws.row_dimensions[1].height = 9
        ws.row_dimensions[2].height = 96
        ws.row_dimensions[3].height = 30

        # Диалог сохранения файла
        default_name = f"{month_name} {year}.xlsx"
        path = filedialog.asksaveasfilename(
            parent=self.root,
            title="Сохранить отчет",
            defaultextension=".xlsx",
            initialfile=default_name,
            filetypes=[("Excel файлы", "*.xlsx"), ("Все файлы", "*.*")],
        )
        if not path:
            return
        try:
            wb.save(path)
        except Exception as e:
            messagebox.showerror("Ошибка", f"Не удалось сохранить отчёт:\n{str(e)}", parent=self.root)
            return
        messagebox.showinfo("Готово", f"Отчёт сохранён:\n{path}", parent=self.root)

    def create_settings_tab(self):
        """Создает интерфейс для вкладки с настройками"""
        settings_frame = tk.Frame(self.tab_settings)
        settings_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # Кнопки для работы с файлом
        self.btn_change_file = tk.Button(settings_frame, text="Выбрать файл", 
                                        command=self.choose_mdb_file, width=20, height=2)
        self.btn_change_file.pack(pady=10)
        
        self.btn_refresh = tk.Button(settings_frame, text="Обновить", 
                                    command=self.load_mdb_data, width=20, height=2)
        self.btn_refresh.pack(pady=10)
        
        self.loading_label = tk.Label(settings_frame, text="", fg="blue")
        self.loading_label.pack(pady=5)
        
        self.lbl_path = tk.Label(settings_frame, 
                                text=f"Файл: {self.mdb_path if self.mdb_path else 'Не выбран'}", 
                                anchor='w', fg="gray", wraplength=800)
        self.lbl_path.pack(pady=10)

    def create_devices_tab(self):
        """Создает интерфейс для вкладки с приборами"""
        # Панель поиска
        search_frame = tk.Frame(self.tab_devices)
        search_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)
        tk.Label(search_frame, text="Поиск:", bg="#f5f6fa").pack(side=tk.LEFT, padx=(0, 5))
        self.devices_search_var = tk.StringVar()
        self.devices_search_entry = ctk.CTkEntry(search_frame, textvariable=self.devices_search_var, height=32)
        self.devices_search_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.devices_search_var.trace_add("write", lambda *a: self.display_data())
        ctk.CTkButton(search_frame, text="Сброс", width=90,
                  command=lambda: self.devices_search_var.set("")).pack(side=tk.LEFT, padx=5)

        table_frame = tk.Frame(self.tab_devices)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        scrollbar_y = tk.Scrollbar(table_frame)
        scrollbar_y.pack(side=tk.RIGHT, fill=tk.Y)
        
        scrollbar_x = tk.Scrollbar(table_frame, orient=tk.HORIZONTAL)
        scrollbar_x.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Добавляем 5-ю колонку для индикатора
        self.tree_devices = ttk.Treeview(
            table_frame, 
            columns=("№", "Название", "Заводской номер", "Действие поверки", "Индикатор"),
            show="headings",
            yscrollcommand=scrollbar_y.set,
            xscrollcommand=scrollbar_x.set
        )
        
        self.tree_devices.heading("№", text="№", command=lambda: self.sort_devices("№"))
        self.tree_devices.heading("Название", text="Название", command=lambda: self.sort_devices("Название"))
        self.tree_devices.heading("Заводской номер", text="Заводской номер",
                                  command=lambda: self.sort_devices("Заводской номер"))
        self.tree_devices.heading("Действие поверки", text="Действие поверки")
        self.tree_devices.heading("Индикатор", text="Индикатор",
                                  command=lambda: self.sort_devices("Индикатор"))
        
        # Устанавливаем ширину колонок
        self.tree_devices.column("№", width=50, anchor='center', minwidth=50, stretch=False)
        self.tree_devices.column("Название", width=400, minwidth=300, stretch=True)
        self.tree_devices.column("Заводской номер", width=150, minwidth=100, anchor='center')
        self.tree_devices.column("Действие поверки", width=190, minwidth=150, anchor='center')
        self.tree_devices.column("Индикатор", width=85, minwidth=75, anchor='center', stretch=False)
        
        scrollbar_y.config(command=self.tree_devices.yview)
        scrollbar_x.config(command=self.tree_devices.xview)
        
        self.tree_devices.pack(fill=tk.BOTH, expand=True)
        
        self.tree_devices.bind("<Button-3>", self.show_devices_context_menu)
        self.tree_devices.bind("<Double-1>", lambda e: self.show_details())
        
        self.devices_context_menu = tk.Menu(self.root, tearoff=0)
        self.devices_context_menu.add_command(label="Дополнительные сведения", command=self.show_details)

        # Состояние сортировки
        self.devices_sort_col = None
        self.devices_sort_desc = False
    
    def create_persons_tab(self):
        """Создает интерфейс для вкладки с персоналом"""
        table_frame = tk.Frame(self.tab_persons)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        scrollbar_y = tk.Scrollbar(table_frame)
        scrollbar_y.pack(side=tk.RIGHT, fill=tk.Y)
        
        scrollbar_x = tk.Scrollbar(table_frame, orient=tk.HORIZONTAL)
        scrollbar_x.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Создаем таблицу для персонала
        self.tree_persons = ttk.Treeview(
            table_frame, 
            columns=("№", "Короткое ФИО", "ФИО", "Должность"),
            show="headings",
            yscrollcommand=scrollbar_y.set,
            xscrollcommand=scrollbar_x.set
        )
        
        self.tree_persons.heading("№", text="№")
        self.tree_persons.heading("Короткое ФИО", text="Короткое ФИО")
        self.tree_persons.heading("ФИО", text="ФИО")
        self.tree_persons.heading("Должность", text="Должность")
        
        # Устанавливаем ширину колонок
        self.tree_persons.column("№", width=50, anchor='center', minwidth=50, stretch=False)
        self.tree_persons.column("Короткое ФИО", width=180, minwidth=140, stretch=False)
        self.tree_persons.column("ФИО", width=300, minwidth=200, stretch=True)
        self.tree_persons.column("Должность", width=250, minwidth=150, stretch=True)
        
        scrollbar_y.config(command=self.tree_persons.yview)
        scrollbar_x.config(command=self.tree_persons.xview)
        
        self.tree_persons.pack(fill=tk.BOTH, expand=True)
    
    def create_kits_tab(self):
        """Создает интерфейс для вкладки с комплектами"""
        # Верхняя панель с кнопками
        top_frame = tk.Frame(self.tab_kits)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)
        
        self.btn_create_kit = ctk.CTkButton(top_frame, text="Создать комплект", 
                                       command=self.create_kit_dialog, width=180)
        self.btn_create_kit.pack(side=tk.LEFT, padx=5)
        
        self.btn_delete_kit = ctk.CTkButton(top_frame, text="Удалить комплект", 
                                       command=self.delete_kit, width=180,
                                       fg_color="transparent", border_width=1,
                                       text_color="#0b5fff", border_color="#0b5fff",
                                       hover_color="#e8f0ff")
        self.btn_delete_kit.pack(side=tk.LEFT, padx=5)
        
        # Панель поиска
        search_frame = tk.Frame(self.tab_kits)
        search_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=(0, 5))
        tk.Label(search_frame, text="Поиск:", bg="#f5f6fa").pack(side=tk.LEFT, padx=(0, 5))
        self.kits_search_var = tk.StringVar()
        self.kits_search_entry = ctk.CTkEntry(search_frame, textvariable=self.kits_search_var, height=32)
        self.kits_search_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.kits_search_var.trace_add("write", lambda *a: self.display_kits())
        ctk.CTkButton(search_frame, text="Сброс", width=90,
                  command=lambda: self.kits_search_var.set("")).pack(side=tk.LEFT, padx=5)
        
        # Таблица с комплектами
        table_frame = tk.Frame(self.tab_kits)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        scrollbar_y = tk.Scrollbar(table_frame)
        scrollbar_y.pack(side=tk.RIGHT, fill=tk.Y)
        
        scrollbar_x = tk.Scrollbar(table_frame, orient=tk.HORIZONTAL)
        scrollbar_x.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Создаем таблицу для комплектов
        self.tree_kits = ttk.Treeview(
            table_frame, 
            columns=("№", "Наименование", "Фактор", "Приборы"),
            show="headings",
            yscrollcommand=scrollbar_y.set,
            xscrollcommand=scrollbar_x.set
        )
        
        self.tree_kits.heading("№", text="№", command=lambda: self.sort_kits("№"))
        self.tree_kits.heading("Наименование", text="Наименование комплекта",
                               command=lambda: self.sort_kits("Наименование"))
        self.tree_kits.heading("Фактор", text="Фактор")
        self.tree_kits.heading("Приборы", text="Приборы")
        
        # Устанавливаем ширину колонок
        self.tree_kits.column("№", width=50, anchor='center', minwidth=50, stretch=False)
        self.tree_kits.column("Наименование", width=200, minwidth=150, stretch=True)
        self.tree_kits.column("Фактор", width=200, minwidth=100, stretch=True)
        self.tree_kits.column("Приборы", width=500, minwidth=300, stretch=True)
        
        scrollbar_y.config(command=self.tree_kits.yview)
        scrollbar_x.config(command=self.tree_kits.xview)
        
        self.tree_kits.pack(fill=tk.BOTH, expand=True)
        
        self.tree_kits.bind("<Button-3>", self.show_kits_context_menu)
        
        self.kits_context_menu = tk.Menu(self.root, tearoff=0)
        self.kits_context_menu.add_command(label="Редактировать", command=self.edit_kit)

        # Состояние сортировки
        self.kits_sort_col = None
        self.kits_sort_desc = False
    
    def create_kit_dialog(self):
        """Открывает диалог создания комплекта"""
        dialog = tk.Toplevel(self.root)
        dialog.title("Создание комплекта")
        dialog.geometry("600x700")
        
        # Пункт 1: Наименование комплекта
        tk.Label(dialog, text="1. Наименование комплекта:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        entry_name = tk.Entry(dialog, width=50)
        entry_name.pack(fill=tk.X, padx=10, pady=5)
        
        # Пункт 2: Измеряемые факторы
        tk.Label(dialog, text="2. Измеряемые факторы:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        
        factors_frame = tk.Frame(dialog)
        factors_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Создаем canvas для прокрутки
        canvas = tk.Canvas(factors_frame)
        scrollbar = tk.Scrollbar(factors_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas)
        
        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Создаем чекбоксы для факторов
        factor_vars = {}
        for factor_id, factor_name in self.factors_dict.items():
            var = tk.BooleanVar()
            factor_vars[factor_id] = var
            # Извлекаем полное наименование (без скобок)
            full_name = re.sub(r'\s*\([^)]*\)', '', factor_name)
            tk.Checkbutton(scrollable_frame, text=f"{factor_id}. {full_name}", 
                          variable=var, anchor='w').pack(fill=tk.X, pady=2)
        
        # Пункт 3: Приборы
        tk.Label(dialog, text="3. Приборы:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        
        self.selected_devices = []
        self.devices_listbox = tk.Listbox(dialog, height=6)
        self.devices_listbox.pack(fill=tk.X, padx=10, pady=5)
        
        btn_select_devices = tk.Button(dialog, text="Выбрать приборы", 
                                      command=lambda: self.select_devices_dialog(factor_vars))
        btn_select_devices.pack(pady=5)
        
        # Кнопка сохранения
        btn_save = tk.Button(dialog, text="Сохранить комплект", 
                            command=lambda: self.save_kit(entry_name.get(), factor_vars, dialog))
        btn_save.pack(pady=10)
    
    def select_devices_dialog(self, factor_vars):
        """Открывает диалог выбора приборов"""
        # Сбрасываем предыдущий выбор при каждом новом открытии окна,
        # чтобы не оставались отмеченные приборы от прошлого комплекта.
        self.device_selection_vars = {}
        # Получаем выбранные факторы
        selected_factors = [str(fid) for fid, var in factor_vars.items() if var.get()]
        
        if not selected_factors:
            self._warn(self.root, "Предупреждение", "Сначала выберите факторы")
            return
        
        dialog = tk.Toplevel(self.root)
        dialog.title("Выбор приборов")
        dialog.geometry("800x600")
        
        # Создаем вкладки
        notebook = ttk.Notebook(dialog)
        notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Вкладка с совпавшими факторами
        tab_matching = tk.Frame(notebook)
        notebook.add(tab_matching, text="Приборы с совпавшими факторами")
        
        # Вкладка со всеми приборами
        tab_all = tk.Frame(notebook)
        notebook.add(tab_all, text="Все приборы")
        
        # Создаем таблицы для обеих вкладок
        self.create_device_selection_table(tab_matching, selected_factors, only_matching=True)
        self.create_device_selection_table(tab_all, selected_factors, only_matching=False)
        
        # Кнопка добавления
        btn_add = tk.Button(dialog, text="Добавить выбранные приборы", 
                           command=lambda: self.add_selected_devices(dialog))
        btn_add.pack(pady=10)
    
    def create_device_selection_table(self, parent, selected_factors, only_matching=True):
        """Создает таблицу для выбора приборов с поиском"""
        # Панель поиска
        search_frame = tk.Frame(parent)
        search_frame.pack(fill=tk.X, padx=5, pady=(5, 0))
        tk.Label(search_frame, text="Поиск:").pack(side=tk.LEFT, padx=(0, 5))
        search_var = tk.StringVar()
        search_entry = tk.Entry(search_frame, textvariable=search_var, width=30)
        search_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)

        table_frame = tk.Frame(parent)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        scrollbar_y = tk.Scrollbar(table_frame)
        scrollbar_y.pack(side=tk.RIGHT, fill=tk.Y)
        
        scrollbar_x = tk.Scrollbar(table_frame, orient=tk.HORIZONTAL)
        scrollbar_x.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Создаем атрибут для хранения выбранных приборов
        if not hasattr(self, 'device_selection_vars'):
            self.device_selection_vars = {}
        
        tree = ttk.Treeview(
            table_frame, 
            columns=("Выбрать", "Название", "Заводской номер", "Факторы"),
            show="headings",
            yscrollcommand=scrollbar_y.set,
            xscrollcommand=scrollbar_x.set
        )
        
        tree.heading("Выбрать", text="Выбрать")
        tree.heading("Название", text="Название")
        tree.heading("Заводской номер", text="Заводской номер")
        tree.heading("Факторы", text="Факторы")
        
        tree.column("Выбрать", width=60, anchor='center', minwidth=60)
        tree.column("Название", width=300, minwidth=200)
        tree.column("Заводской номер", width=150, minwidth=100)
        tree.column("Факторы", width=200, minwidth=100)
        
        scrollbar_y.config(command=tree.yview)
        scrollbar_x.config(command=tree.xview)
        
        tree.pack(fill=tk.BOTH, expand=True)
        
        # Получаем все приборы
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        cursor.execute("SELECT id, name, factory_num, fac_ids FROM devices")
        devices = cursor.fetchall()
        conn.close()

        # Заполнение таблицы с учётом поиска
        def populate():
            for item in tree.get_children():
                tree.delete(item)
            query = (search_var.get() or "").strip().lower()
            for device in devices:
                device_id, name, factory_num, fac_ids = device

                # Проверяем совпадение факторов
                has_matching_factor = False
                if fac_ids:
                    device_factors = re.findall(r'\d+', fac_ids)
                    for factor in selected_factors:
                        if factor in device_factors:
                            has_matching_factor = True
                            break

                # Показываем прибор в зависимости от фильтра
                if only_matching and not has_matching_factor:
                    continue

                name_disp = name or "Без названия"
                factory_disp = factory_num or "-"

                # Фильтр по поиску
                if query and (query not in name_disp.lower() and query not in factory_disp.lower()):
                    continue

                # Форматируем факторы для отображения
                factors_display = self.format_factors(fac_ids) if fac_ids else ""

                # Добавляем чекбокс
                if device_id not in self.device_selection_vars:
                    self.device_selection_vars[device_id] = tk.BooleanVar()
                var = self.device_selection_vars[device_id]

                tree.insert("", "end", iid=device_id, values=(
                    "☑" if var.get() else "☐", name_disp, factory_disp, factors_display
                ))

        populate()
        search_var.trace_add("write", lambda *a: populate())
        
        # Обработка кликов для чекбоксов
        def on_click(event):
            item = tree.identify_row(event.y)
            if item:
                col = tree.identify_column(event.x)
                if col == '#1':  # Колонка "Выбрать"
                    var = self.device_selection_vars.get(item)
                    if var:
                        var.set(not var.get())
                        tree.set(item, "Выбрать", "☑" if var.get() else "☐")
        
        tree.bind("<Button-1>", on_click)
        
        # Сохраняем ссылку на дерево
        if only_matching:
            self.tree_devices_matching = tree
        else:
            self.tree_devices_all = tree
    
    def add_selected_devices(self, dialog):
        """Добавляет выбранные приборы в список"""
        self.selected_devices = []
        
        for device_id, var in self.device_selection_vars.items():
            if var.get():
                # Получаем название прибора
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                cursor.execute("SELECT name FROM devices WHERE id = ?", (device_id,))
                result = cursor.fetchone()
                conn.close()
                
                if result:
                    self.selected_devices.append((device_id, result[0]))
        
        # Обновляем список в основном диалоге
        if hasattr(self, 'devices_listbox'):
            self.devices_listbox.delete(0, tk.END)
            for device_id, device_name in self.selected_devices:
                self.devices_listbox.insert(tk.END, device_name)
        
        dialog.destroy()
    
    def save_kit(self, name, factor_vars, dialog):
        """Сохраняет комплект в базу данных"""
        if not name:
            self._warn(dialog, "Ошибка", "Введите наименование комплекта", "error")
            return
        
        # Получаем выбранные факторы
        selected_factors = [str(fid) for fid, var in factor_vars.items() if var.get()]
        
        if not selected_factors:
            self._warn(dialog, "Ошибка", "Выберите хотя бы один фактор", "error")
            return
        
        if not self.selected_devices:
            self._warn(dialog, "Ошибка", "Выберите приборы для комплекта", "error")
            return
        
        # Сохраняем в базу данных
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Сохраняем комплект
        factors_str = ",".join(selected_factors)
        cursor.execute('''
            INSERT INTO kits (name, factors)
            VALUES (?, ?)
        ''', (name, factors_str))
        
        kit_id = cursor.lastrowid
        
        # Сохраняем приборы комплекта
        for device_id, device_name in self.selected_devices:
            cursor.execute('''
                INSERT INTO kit_devices (kit_id, device_id)
                VALUES (?, ?)
            ''', (kit_id, device_id))
        
        conn.commit()
        conn.close()
        
        dialog.destroy()
        self.display_kits()
        messagebox.showinfo("Успех", "Комплект создан")
    
    def delete_kit(self):
        """Удаляет выбранный комплект"""
        selection = self.tree_kits.selection()
        if not selection:
            messagebox.showwarning("Предупреждение", "Выберите комплект для удаления")
            return
        
        if messagebox.askyesno("Подтверждение", "Удалить выбранный комплект?"):
            kit_id = selection[0]
            
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            # Удаляем комплект и связанные записи
            cursor.execute("DELETE FROM kit_devices WHERE kit_id = ?", (kit_id,))
            cursor.execute("DELETE FROM kits WHERE id_complect = ?", (kit_id,))
            
            conn.commit()
            conn.close()
            
            self.display_kits()
    
    def edit_kit(self):
        """Редактирует выбранный комплект"""
        selection = self.tree_kits.selection()
        if not selection:
            return
        
        kit_id = selection[0]
        
        # Получаем данные комплекта
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        cursor.execute("SELECT name, factors FROM kits WHERE id_complect = ?", (kit_id,))
        kit_data = cursor.fetchone()
        
        cursor.execute("""
            SELECT device_id FROM kit_devices WHERE kit_id = ?
        """, (kit_id,))
        kit_devices = [row[0] for row in cursor.fetchall()]
        conn.close()
        
        if not kit_data:
            return
        
        # Открываем диалог редактирования (аналогично созданию)
        self.edit_kit_dialog(kit_id, kit_data[0], kit_data[1], kit_devices)
    
    def edit_kit_dialog(self, kit_id, name, factors, devices):
        """Открывает диалог редактирования комплекта"""
        dialog = tk.Toplevel(self.root)
        dialog.title("Редактирование комплекта")
        dialog.geometry("600x700")
        
        # Пункт 1: Наименование комплекта
        tk.Label(dialog, text="1. Наименование комплекта:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        entry_name = tk.Entry(dialog, width=50)
        entry_name.insert(0, name)
        entry_name.pack(fill=tk.X, padx=10, pady=5)
        
        # Пункт 2: Измеряемые факторы
        tk.Label(dialog, text="2. Измеряемые факторы:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        
        factors_frame = tk.Frame(dialog)
        factors_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        canvas = tk.Canvas(factors_frame)
        scrollbar = tk.Scrollbar(factors_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas)
        
        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Создаем чекбоксы для факторов
        factor_vars = {}
        selected_factors_list = factors.split(",") if factors else []
        
        for factor_id, factor_name in self.factors_dict.items():
            var = tk.BooleanVar(value=str(factor_id) in selected_factors_list)
            factor_vars[factor_id] = var
            full_name = re.sub(r'\s*\([^)]*\)', '', factor_name)
            tk.Checkbutton(scrollable_frame, text=f"{factor_id}. {full_name}", 
                          variable=var, anchor='w').pack(fill=tk.X, pady=2)
        
        # Пункт 3: Приборы
        tk.Label(dialog, text="3. Приборы:", anchor='w').pack(fill=tk.X, padx=10, pady=5)
        
        self.selected_devices = []
        self.devices_listbox = tk.Listbox(dialog, height=6)
        self.devices_listbox.pack(fill=tk.X, padx=10, pady=5)
        
        # Загружаем текущие приборы
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        for device_id in devices:
            cursor.execute("SELECT name FROM devices WHERE id = ?", (device_id,))
            result = cursor.fetchone()
            if result:
                self.selected_devices.append((device_id, result[0]))
                self.devices_listbox.insert(tk.END, result[0])
        conn.close()
        
        btn_select_devices = tk.Button(dialog, text="Выбрать приборы", 
                                      command=lambda: self.select_devices_dialog(factor_vars))
        btn_select_devices.pack(pady=5)
        
        # Кнопка сохранения
        btn_save = tk.Button(dialog, text="Сохранить изменения", 
                            command=lambda: self.update_kit(kit_id, entry_name.get(), factor_vars, dialog))
        btn_save.pack(pady=10)
    
    def update_kit(self, kit_id, name, factor_vars, dialog):
        """Обновляет комплект в базе данных"""
        if not name:
            self._warn(dialog, "Ошибка", "Введите наименование комплекта", "error")
            return
        
        selected_factors = [str(fid) for fid, var in factor_vars.items() if var.get()]
        
        if not selected_factors:
            self._warn(dialog, "Ошибка", "Выберите хотя бы один фактор", "error")
            return
        
        if not self.selected_devices:
            self._warn(dialog, "Ошибка", "Выберите приборы для комплекта", "error")
            return
        
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Обновляем комплект
        factors_str = ",".join(selected_factors)
        cursor.execute('''
            UPDATE kits SET name = ?, factors = ? WHERE id_complect = ?
        ''', (name, factors_str, kit_id))
        
        # Удаляем старые приборы
        cursor.execute("DELETE FROM kit_devices WHERE kit_id = ?", (kit_id,))
        
        # Добавляем новые приборы
        for device_id, device_name in self.selected_devices:
            cursor.execute('''
                INSERT INTO kit_devices (kit_id, device_id)
                VALUES (?, ?)
            ''', (kit_id, device_id))
        
        conn.commit()
        conn.close()
        
        dialog.destroy()
        self.display_kits()
        messagebox.showinfo("Успех", "Комплект обновлен")
    
    def sort_kits(self, column):
        """Переключает/применяет сортировку таблицы комплектов."""
        if self.kits_sort_col == column:
            self.kits_sort_desc = not self.kits_sort_desc
        else:
            self.kits_sort_col = column
            self.kits_sort_desc = False
        self.display_kits()

    def display_kits(self):
        """Отображает комплекты в таблице с учётом поиска и сортировки"""
        # Очищаем старые элементы
        for item in self.tree_kits.get_children():
            self.tree_kits.delete(item)

        query = ""
        if hasattr(self, 'kits_search_var'):
            query = (self.kits_search_var.get() or "").strip().lower()

        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cursor.execute("""
            SELECT id_complect, name, factors FROM kits
        """)

        kits = cursor.fetchall()

        prepared = []
        for kit in kits:
            kit_id, name, factors = kit

            # Форматируем факторы
            factors_display = ""
            if factors:
                factor_list = factors.split(",")
                factor_names = []
                for factor_id in factor_list:
                    try:
                        fid = int(factor_id)
                    except ValueError:
                        factor_names.append(factor_id)
                        continue
                    if fid in self.factors_dict:
                        full_name = re.sub(r'\s*\([^)]*\)', '', self.factors_dict[fid])
                        factor_names.append(full_name)
                    else:
                        factor_names.append(factor_id)
                factors_display = ", ".join(factor_names)

            # Получаем приборы комплекта
            cursor.execute("""
                SELECT d.name FROM devices d
                JOIN kit_devices kd ON d.id = kd.device_id
                WHERE kd.kit_id = ?
                ORDER BY d.name
            """, (kit_id,))

            device_names = [row[0] for row in cursor.fetchall()]
            # Один приборный список одной строкой (через запятую), чтобы все
            # приборы были видны; горизонтальная прокрутка уже подключена.
            devices_display = ", ".join(device_names)

            name_disp = name or ""
            prepared.append({
                'id': kit_id,
                'name': name_disp,
                'values': (0, name_disp, factors_display, devices_display),
            })

        conn.close()

        # Фильтр по поиску (по наименованию комплекта)
        if query:
            prepared = [p for p in prepared if query in p['name'].lower()]

        # Сортировка
        col = getattr(self, 'kits_sort_col', None)
        desc = getattr(self, 'kits_sort_desc', False)
        if col == "№":
            prepared.sort(key=lambda p: p['id'], reverse=desc)
        elif col == "Наименование":
            prepared.sort(key=lambda p: p['name'].lower(), reverse=desc)
        else:
            prepared.sort(key=lambda p: p['id'])

        # Вставка с перенумерацией
        for i, p in enumerate(prepared, 1):
            values = list(p['values'])
            values[0] = i
            self.tree_kits.insert("", "end", iid=str(p['id']), values=tuple(values))
    
    def read_mdb_via_odbc(self):
        """Чтение MDB через встроенный ODBC API Windows"""
        devices = []
        persons = []
        
        try:
            odbc32 = ctypes.windll.odbc32
            
            henv = ctypes.c_void_p()
            ret = odbc32.SQLAllocHandle(1, None, ctypes.byref(henv))
            
            if ret != 0:
                raise Exception("Не удалось инициализировать ODBC")
            
            odbc32.SQLSetEnvAttr(henv, 200, 3, 0)
            
            hdbc = ctypes.c_void_p()
            ret = odbc32.SQLAllocHandle(2, henv, ctypes.byref(hdbc))
            
            if ret != 0:
                raise Exception("Не удалось создать соединение")
            
            conn_str = f"DRIVER={{Microsoft Access Driver (*.mdb, *.accdb)}};DBQ={self.mdb_path};ReadOnly=1;"
            
            ret = odbc32.SQLDriverConnectW(
                hdbc, None, conn_str, len(conn_str),
                None, 0, None, 0
            )
            
            if ret not in [0, 1]:
                conn_str = f"DRIVER={{Microsoft Access Driver (*.mdb)}};DBQ={self.mdb_path};ReadOnly=1;"
                ret = odbc32.SQLDriverConnectW(
                    hdbc, None, conn_str, len(conn_str),
                    None, 0, None, 0
                )
                
                if ret not in [0, 1]:
                    raise Exception("Не удалось подключиться к базе данных")
            
            devices = self._read_devices(odbc32, hdbc)
            tests_dict = self._read_tests(odbc32, hdbc)
            persons = self._read_persons(odbc32, hdbc)
            
            # Объединяем данные
            for device in devices:
                dev_id = device['id']
                device_tests = tests_dict.get(dev_id, [])
                device['tests'] = device_tests
            
            odbc32.SQLDisconnect(hdbc)
            odbc32.SQLFreeHandle(2, hdbc)
            odbc32.SQLFreeHandle(1, henv)
            
            return devices, persons
            
        except Exception as e:
            messagebox.showerror("Ошибка", f"Ошибка при чтении MDB:\n{str(e)}")
            return None, None
    
    def _read_devices(self, odbc32, hdbc):
        """Чтение таблицы ATT_DEVICE"""
        devices = []
        hstmt = ctypes.c_void_p()
        
        try:
            ret = odbc32.SQLAllocHandle(3, hdbc, ctypes.byref(hstmt))
            
            query = "SELECT id, name, reg_num, factory_num, no_test, fac_ids, si_err, dop_fld3 FROM ATT_DEVICE"
            ret = odbc32.SQLExecDirectW(hstmt, query, len(query))
            
            if ret in [0, 1]:
                while True:
                    ret = odbc32.SQLFetch(hstmt)
                    if ret not in [0, 1]:
                        break
                    
                    values = []
                    for i in range(1, 9):
                        buffer = ctypes.create_unicode_buffer(4096)
                        ind = ctypes.c_long()
                        
                        ret = odbc32.SQLGetData(hstmt, i, -8, buffer, 8192, ctypes.byref(ind))
                        
                        if ret in [0, 1] and ind.value != -1:
                            values.append(buffer.value)
                        else:
                            values.append("")
                    
                    if values[0]:
                        devices.append({
                            'id': values[0],
                            'name': values[1],
                            'reg_num': values[2],
                            'factory_num': values[3],
                            'no_test': int(values[4]) if values[4] else 0,
                            'fac_ids': values[5],
                            'si_err': values[6],
                            'dop_fld3': values[7],
                            'tests': []
                        })
            
        finally:
            if hstmt:
                odbc32.SQLFreeHandle(3, hstmt)
        
        return devices
    
    def _read_tests(self, odbc32, hdbc):
        """Чтение таблицы ATT_DEVICE_TEST"""
        tests_dict = {}
        hstmt = ctypes.c_void_p()
        
        try:
            ret = odbc32.SQLAllocHandle(3, hdbc, ctypes.byref(hstmt))
            
            query = "SELECT dev_id, org_reg, num_doc, begin_date, end_date FROM ATT_DEVICE_TEST ORDER BY begin_date"
            ret = odbc32.SQLExecDirectW(hstmt, query, len(query))
            
            if ret in [0, 1]:
                while True:
                    ret = odbc32.SQLFetch(hstmt)
                    if ret not in [0, 1]:
                        break
                    
                    values = []
                    for i in range(1, 6):
                        buffer = ctypes.create_unicode_buffer(4096)
                        ind = ctypes.c_long()
                        
                        ret = odbc32.SQLGetData(hstmt, i, -8, buffer, 8192, ctypes.byref(ind))
                        
                        if ret in [0, 1] and ind.value != -1:
                            values.append(buffer.value)
                        else:
                            values.append("")
                    
                    if values[0]:
                        dev_id = values[0]
                        if dev_id not in tests_dict:
                            tests_dict[dev_id] = []
                        
                        tests_dict[dev_id].append({
                            'org_reg': values[1],
                            'num_doc': values[2],
                            'begin_date': values[3],
                            'end_date': values[4]
                        })
            
        finally:
            if hstmt:
                odbc32.SQLFreeHandle(3, hstmt)
        
        return tests_dict
    
    def _read_persons(self, odbc32, hdbc):
        """Чтение таблицы ATT_PERSON"""
        persons = []
        hstmt = ctypes.c_void_p()
        
        try:
            ret = odbc32.SQLAllocHandle(3, hdbc, ctypes.byref(hstmt))
            
            query = "SELECT id, fio, dolg FROM ATT_PERSON"
            ret = odbc32.SQLExecDirectW(hstmt, query, len(query))
            
            if ret in [0, 1]:
                while True:
                    ret = odbc32.SQLFetch(hstmt)
                    if ret not in [0, 1]:
                        break
                    
                    values = []
                    for i in range(1, 4):
                        buffer = ctypes.create_unicode_buffer(4096)
                        ind = ctypes.c_long()
                        
                        ret = odbc32.SQLGetData(hstmt, i, -8, buffer, 8192, ctypes.byref(ind))
                        
                        if ret in [0, 1] and ind.value != -1:
                            values.append(buffer.value)
                        else:
                            values.append("")
                    
                    if values[0]:
                        persons.append({
                            'id_person': values[0],
                            'fio': values[1],
                            'dolg': values[2]
                        })
            
        finally:
            if hstmt:
                odbc32.SQLFreeHandle(3, hstmt)
        
        return persons
    
    def choose_mdb_file(self):
        if self.is_loading:
            messagebox.showwarning("Предупреждение", "Дождитесь завершения загрузки")
            return
        
        file_path = filedialog.askopenfilename(
            title="Выберите файл базы данных",
            filetypes=[("MS Access Database", "*.mdb"), ("All files", "*.*")]
        )
        
        if file_path:
            self.mdb_path = file_path
            self.save_config()
            self.lbl_path.config(text=f"Файл: {self.mdb_path}")
            
            # При выборе файла всегда загружаем данные
            self.load_mdb_data()
        elif self.first_run:
            # Если пользователь отказался выбирать файл при первом запуске
            messagebox.showwarning("Предупреждение", "Для работы приложения необходимо выбрать файл базы данных")
    
    def load_mdb_data(self):
        if self.is_loading:
            messagebox.showwarning("Предупреждение", "Загрузка уже выполняется")
            return
        
        if not self.mdb_path:
            self.choose_mdb_file()
            return
        
        if not os.path.exists(self.mdb_path):
            messagebox.showerror("Ошибка", "Файл базы данных не найден")
            return
        
        self.is_loading = True
        self.btn_refresh.config(state=tk.DISABLED)
        self.btn_change_file.config(state=tk.DISABLED)
        self.loading_label.config(text="Загрузка...")
        
        thread = threading.Thread(target=self._load_mdb_data_thread)
        thread.daemon = True
        thread.start()
    
    def _load_mdb_data_thread(self):
        devices, persons = self.read_mdb_via_odbc()
        
        if devices is None and persons is None:
            self.root.after(0, self._reset_loading_state)
            return
        
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            # Очищаем только таблицы с данными из MDB
            cursor.execute("DELETE FROM device_tests")
            cursor.execute("DELETE FROM devices")
            cursor.execute("DELETE FROM persons")
            
            # Вставляем данные о приборах
            if devices:
                for device in devices:
                    data_string = json.dumps(device, sort_keys=True, ensure_ascii=False)
                    data_hash = hashlib.md5(data_string.encode('utf-8')).hexdigest()
                    
                    cursor.execute('''
                        INSERT INTO devices 
                        (id, name, reg_num, factory_num, no_test, fac_ids, si_err, dop_fld3, 
                         active, data_hash)
                        VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?)
                    ''', (
                        device['id'], device['name'], device['reg_num'], device['factory_num'],
                        device['no_test'], device['fac_ids'], device['si_err'], device['dop_fld3'],
                        data_hash
                    ))
                    
                    # Вставляем все поверки
                    for test in device['tests']:
                        cursor.execute('''
                            INSERT INTO device_tests 
                            (dev_id, org_reg, num_doc, begin_date, end_date)
                            VALUES (?, ?, ?, ?, ?)
                        ''', (
                            device['id'], test['org_reg'], test['num_doc'],
                            test['begin_date'], test['end_date']
                        ))
            
            # Вставляем данные о персонале
            if persons:
                for person in persons:
                    cursor.execute('''
                        INSERT INTO persons 
                        (id_person, fio, dolg)
                        VALUES (?, ?, ?)
                    ''', (
                        person['id_person'], person['fio'], person['dolg']
                    ))
            
            conn.commit()
            
            # Обновляем статусы active на основе дат
            if devices:
                self.update_active_status(cursor)
            
            conn.commit()
            conn.close()
            
            self.first_run = False
            total_count = len(devices) if devices else 0
            persons_count = len(persons) if persons else 0
            self.root.after(0, lambda: self._on_load_success(total_count, persons_count))
            
        except Exception as e:
            self.root.after(0, lambda: messagebox.showerror("Ошибка", f"Ошибка при сохранении:\n{str(e)}"))
            self.root.after(0, self._reset_loading_state)
    
    def update_active_status(self, cursor):
        """Обновляет поле active для всех устройств на основе дат поверки"""
        today = date.today()
        
        # Получаем все устройства
        cursor.execute("SELECT id FROM devices")
        devices = cursor.fetchall()
        
        for device in devices:
            device_id = device[0]
            
            # Получаем последнюю поверку для устройства
            cursor.execute("""
                SELECT end_date FROM device_tests
                WHERE dev_id = ?
                ORDER BY end_date DESC
                LIMIT 1
            """, (device_id,))
            
            result = cursor.fetchone()
            
            active = 1  # По умолчанию активен
            
            if result and result[0]:
                try:
                    end_date = datetime.strptime(result[0], '%Y-%m-%d').date()
                    if end_date < today:
                        active = 0  # Поверка истекла
                except:
                    pass  # Если дата некорректна, оставляем active = 1
            
            cursor.execute("UPDATE devices SET active = ? WHERE id = ?", (active, device_id))
    
    def update_indicators_only(self):
        """Обновляет только индикаторы и поле active без перезагрузки данных"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            # Обновляем статусы active
            self.update_active_status(cursor)
            conn.commit()
            conn.close()
            
            # Обновляем отображение
            self.display_data()
            self.display_persons()
            self.display_kits()
            
        except Exception as e:
            messagebox.showerror("Ошибка", f"Ошибка при обновлении индикаторов:\n{str(e)}")
    
    def _on_load_success(self, devices_count, persons_count):
        self.display_data()
        self.display_persons()
        self.display_kits()
        self._reset_loading_state()
    
    def _reset_loading_state(self):
        self.is_loading = False
        self.btn_refresh.config(state=tk.NORMAL)
        self.btn_change_file.config(state=tk.NORMAL)
        self.loading_label.config(text="")
    
    def format_date(self, date_str):
        """Преобразует дату из формата YYYY-MM-DD в DD.MM.YYYY"""
        return utils.format_date(date_str)
    
    def get_indicator_symbol(self, end_date_str):
        """Определяет символ индикатора на основе даты окончания поверки"""
        return utils.get_indicator_symbol(end_date_str)

    def short_fio(self, fio):
        """Возвращает короткое ФИО («Иванов И.И.»)."""
        return utils.short_fio(fio)
    
    def format_factors(self, fac_ids):
        """Расшифровывает номера факторов в читаемый вид"""
        return utils.format_factors(fac_ids, self.factors_dict)
    
    def get_vri_id(self, num_doc):
        """Получает vri_id из ФГИС Аршин по номеру документа"""
        try:
            # Формируем поисковый запрос с номером документа
            search_query = f"*{num_doc}*"
            
            url = "https://fgis.gost.ru/fundmetrology/eapi/vri"
            params = {
                'search': search_query,
                'rows': 10,
                'start': 0,
                'verification_date_start': '2016-01-01'
            }
            
            resp = requests.get(url, params=params, timeout=30)
            data = resp.json()
            
            # Проверяем наличие результатов
            if 'result' in data and 'items' in data['result']:
                items = data['result']['items']
                if items:
                    # Возвращаем первый найденный vri_id
                    return items[0].get('vri_id')
            
            return None
            
        except Exception as e:
            print(f"Ошибка при получении vri_id: {e}")
            return None
    
    def open_fgis_link(self, num_doc):
        """Открывает ссылку на ФГИС Аршин в браузере"""
        vri_id = self.get_vri_id(num_doc)
        
        if vri_id:
            url = f"https://fgis.gost.ru/fundmetrology/cm/results/{vri_id}"
            webbrowser.open(url)
        else:
            messagebox.showwarning("Предупреждение", 
                                 f"Не удалось найти информацию о поверке с номером:\n{num_doc}")
    
    def device_indicator_bucket(self, active, end_date):
        """Возвращает ключ для кастомной сортировки индикатора."""
        return utils.device_indicator_bucket(active, end_date)

    def fetch_devices_rows(self):
        """Возвращает список строк приборов как кортежей
        (device_id, name, factory_num, no_test, active, begin_date, end_date)."""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        cursor.execute("""
            SELECT d.id, d.name, d.factory_num, d.no_test, d.active,
                   (SELECT dt.begin_date FROM device_tests dt 
                    WHERE dt.dev_id = d.id 
                    ORDER BY dt.begin_date DESC LIMIT 1) as begin_date,
                   (SELECT dt.end_date FROM device_tests dt 
                    WHERE dt.dev_id = d.id 
                    ORDER BY dt.begin_date DESC LIMIT 1) as end_date
            FROM devices d
        """)
        rows = cursor.fetchall()
        conn.close()
        return rows

    def sort_devices(self, column):
        """Переключает/применяет сортировку таблицы приборов по колонке."""
        if self.devices_sort_col == column:
            self.devices_sort_desc = not self.devices_sort_desc
        else:
            self.devices_sort_col = column
            self.devices_sort_desc = False
        self.display_data()

    def display_data(self):
        """Отображает данные о приборах с учётом поиска и сортировки"""
        # Очищаем старые элементы
        for item in self.tree_devices.get_children():
            self.tree_devices.delete(item)

        query = ""
        if hasattr(self, 'devices_search_var'):
            query = (self.devices_search_var.get() or "").strip().lower()

        devices = self.fetch_devices_rows()

        prepared = []
        for row in devices:
            device_id, name, factory_num, no_test, active, begin_date, end_date = row

            # Формируем строку с датами поверки
            if begin_date and end_date:
                formatted_begin = self.format_date(begin_date)
                formatted_end = self.format_date(end_date)
                date_range = f"{formatted_begin}-{formatted_end}"
            elif begin_date:
                formatted_begin = self.format_date(begin_date)
                date_range = f"{formatted_begin}-"
            elif end_date:
                formatted_end = self.format_date(end_date)
                date_range = f"-{formatted_end}"
            else:
                date_range = ""

            # Определяем символ индикатора
            if active == 0:
                indicator_symbol = "✖"
            else:
                indicator_symbol = self.get_indicator_symbol(end_date)

            name_disp = name or "Без названия"
            factory_disp = factory_num or "-"
            bucket = self.device_indicator_bucket(active, end_date)

            prepared.append({
                'id': device_id,
                'name': name_disp,
                'factory': factory_disp,
                'bucket': bucket,
                'values': (0, name_disp, factory_disp, date_range, indicator_symbol),
            })

        # Фильтр по поиску (наименование или заводской номер)
        if query:
            prepared = [p for p in prepared
                        if query in p['name'].lower() or query in p['factory'].lower()]

        # Сортировка
        col = getattr(self, 'devices_sort_col', None)
        desc = getattr(self, 'devices_sort_desc', False)
        if col == "№":
            def num_key(p):
                try:
                    return int(p['id'])
                except (ValueError, TypeError):
                    return p['id']
            prepared.sort(key=num_key, reverse=desc)
        elif col == "Название":
            prepared.sort(key=lambda p: p['name'].lower(), reverse=desc)
        elif col == "Заводской номер":
            def factory_key(p):
                s = p['factory']
                try:
                    return (0, int(s))
                except (ValueError, TypeError):
                    return (1, s.lower())
            prepared.sort(key=factory_key, reverse=desc)
        elif col == "Индикатор":
            prepared.sort(key=lambda p: p['bucket'], reverse=desc)
        else:
            prepared.sort(key=lambda p: p['name'].lower(), reverse=desc)

        # Вставка с перенумерацией
        for i, p in enumerate(prepared, 1):
            values = list(p['values'])
            values[0] = i
            self.tree_devices.insert("", "end", iid=p['id'], values=tuple(values))
    
    def display_persons(self):
        """Отображает данные о персонале"""
        # Очищаем старые элементы
        for item in self.tree_persons.get_children():
            self.tree_persons.delete(item)
        
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute("""
            SELECT id_person, fio, dolg
            FROM persons
            ORDER BY fio
        """)
        
        persons = cursor.fetchall()
        conn.close()
        
        for i, person in enumerate(persons, 1):
            id_person, fio, dolg = person
            
            # Вставляем элемент (с коротким ФИО)
            self.tree_persons.insert("", "end", values=(
                i,
                self.short_fio(fio),
                fio or "", 
                dolg or ""
            ))
    
    def show_devices_context_menu(self, event):
        item = self.tree_devices.identify_row(event.y)
        if item:
            self.tree_devices.selection_set(item)
            self.devices_context_menu.post(event.x_root, event.y_root)
    
    def show_kits_context_menu(self, event):
        item = self.tree_kits.identify_row(event.y)
        if item:
            self.tree_kits.selection_set(item)
            self.kits_context_menu.post(event.x_root, event.y_root)
    
    def show_details(self):
        selection = self.tree_devices.selection()
        if not selection:
            return
        
        item = self.tree_devices.item(selection[0])
        values = item.get('values', [])
        
        if len(values) < 1:
            return
        
        # Получаем ID устройства из базы данных по названию
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Находим ID по названию и заводскому номеру
        cursor.execute("""
            SELECT id FROM devices 
            WHERE name = ? AND factory_num = ?
            LIMIT 1
        """, (values[1], values[2]))
        
        result = cursor.fetchone()
        if not result:
            conn.close()
            return
        
        device_id = result[0]
        
        cursor.execute("SELECT * FROM devices WHERE id = ?", (device_id,))
        device = cursor.fetchone()
        
        cursor.execute("""
            SELECT org_reg, num_doc, begin_date, end_date
            FROM device_tests
            WHERE dev_id = ?
            ORDER BY begin_date DESC
        """, (device_id,))
        
        tests = cursor.fetchall()
        conn.close()
        
        if not device:
            messagebox.showerror("Ошибка", "Устройство не найдено")
            return
        
        details_window = tk.Toplevel(self.root)
        details_window.title("Дополнительные сведения")
        details_window.geometry("700x600")
        
        frame = tk.Frame(details_window)
        frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        scrollbar = tk.Scrollbar(frame)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        text_widget = tk.Text(frame, wrap=tk.WORD, padx=10, pady=10,
                              yscrollcommand=scrollbar.set, font=("Arial", 10))
        text_widget.pack(fill=tk.BOTH, expand=True)
        scrollbar.config(command=text_widget.yview)
        
        # Получаем значения полей
        device_id_val = device[0]
        name = device[1] if device[1] else 'Не указано'
        reg_num = device[2] if device[2] else 'Не указан'
        factory_num = device[3] if device[3] else 'Не указан'
        no_test = device[4]
        fac_ids = device[5] if device[5] else 'Не указаны'
        si_err = device[6] if device[6] else 'Не указана'
        dop_fld3 = device[7] if device[7] else 'Не указано'
        active = device[8]
        
        # Расшифровываем факторы
        fac_ids_decoded = self.format_factors(fac_ids) if fac_ids != 'Не указаны' else 'Не указаны'
        
        details_text = f"""ДОПОЛНИТЕЛЬНЫЕ СВЕДЕНИЯ О ПРИБОРЕ

Основная информация:
• ID: {device_id_val}
• Название: {name}
• Заводской номер: {factory_num}
• Регистрационный номер: {reg_num}

Статус поверки:
• Наличие поверки: {'С поверкой' if no_test == 1 else 'Без поверки'}
• Статус: {'Активен' if active == 1 else 'Поверка истекла'}

Технические характеристики:
• Факторы: {fac_ids_decoded}
• Погрешность: {si_err}
• Условия эксплуатации: {dop_fld3}

История поверок:
"""
        
        text_widget.insert("1.0", details_text)
        
        # Добавляем кнопки-ссылки для каждой поверки
        if tests:
            for i, test in enumerate(tests, 1):
                # Форматируем даты
                begin_formatted = self.format_date(test[2]) if test[2] else 'Не указана'
                end_formatted = self.format_date(test[3]) if test[3] else 'Не указана'
                
                # Добавляем заголовок поверки
                text_widget.insert(tk.END, f"\nПоверка {i}:\n")
                
                # Добавляем информацию о поверке
                text_widget.insert(tk.END, f"• Орган поверитель: {test[0] if test[0] else 'Не указан'}\n")
                text_widget.insert(tk.END, f"• Номер документа: {test[1] if test[1] else 'Не указан'}\n")
                text_widget.insert(tk.END, f"• Дата начала: {begin_formatted}\n")
                text_widget.insert(tk.END, f"• Дата конца: {end_formatted}\n")
                
                # Добавляем кнопку-ссылку если есть номер документа
                if test[1]:
                    # Создаем кнопку с иконкой ссылки
                    link_button = tk.Button(text_widget, text="Открыть в ФГИС Аршин",
                                          command=lambda doc=test[1]: self.open_fgis_link(doc),
                                          cursor="hand2", relief=tk.FLAT, 
                                          fg="blue", font=("Arial", 9, "underline"))
                    text_widget.window_create(tk.END, window=link_button)
                    text_widget.insert(tk.END, "\n")
        else:
            text_widget.insert(tk.END, "\nНет данных о поверках")
        
        text_widget.config(state=tk.DISABLED)
        
        # Делаем текст доступным для выделения и копирования
        text_widget.bind("<Button-1>", lambda e: self.enable_text_selection(text_widget))
        text_widget.bind("<B1-Motion>", lambda e: self.enable_text_selection(text_widget))
        
        btn_close = tk.Button(details_window, text="Закрыть",
                             command=details_window.destroy)
        btn_close.pack(pady=10)
    
    def enable_text_selection(self, text_widget):
        """Временно включает возможность выделения текста для копирования"""
        text_widget.config(state=tk.NORMAL)
        # Планируем отключение после выделения
        self.root.after(100, lambda: self.disable_text_selection(text_widget))
    
    def disable_text_selection(self, text_widget):
        """Отключает редактирование текста, сохраняя возможность копирования"""
        text_widget.config(state=tk.DISABLED)


# =====================================================================
# Диалоги выездов
# =====================================================================


def main():
    root = ctk.CTk()
    app = MDBViewerApp(root)
    root.mainloop()

if __name__ == "__main__":
    main()
#!/usr/bin/env python3
"""Interface graphique Qt6 (PySide6) pour hh_profile_stats.py

Installation :  pip install PySide6
Lancement    :  python hh_profile_stats_gui.py
Glisser-deposer d'un dossier ou de fichiers HH_Profile*.txt dans la fenetre possible.
"""
import sys, os, csv
from PySide6.QtCore import Qt, QSettings
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (QApplication, QFileDialog, QHBoxLayout, QLabel, QLineEdit,
                               QMainWindow, QPushButton, QTableWidget, QTableWidgetItem,
                               QVBoxLayout, QWidget, QHeaderView, QMessageBox)
import hh_profile_stats as core

HEADERS = ["Ligne de profil", "Mains", "Net", "Moy./main", "Mains (dernière)", "Net (dernière)"]

class NumItem(QTableWidgetItem):
    """Cellule numerique triable."""
    def __init__(self, value, text):
        super().__init__(text)
        self._v = value
        self.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
    def __lt__(self, other):
        return self._v < other._v

class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("OpenHoldem - Analyse du profil (HH + lignes)")
        self.resize(1100, 600)
        self.setAcceptDrops(True)
        self.settings = QSettings("OpenHoldem", "HHProfileStats")
        self.sources = []
        self.rows = []

        root = QWidget(); self.setCentralWidget(root)
        lay = QVBoxLayout(root)
        bar = QHBoxLayout(); lay.addLayout(bar)
        for text, slot in (("Ouvrir dossier…", self.open_folder), ("Ouvrir fichier(s)…", self.open_files),
                           ("Recharger", self.reload), ("Exporter CSV…", self.export_csv)):
            b = QPushButton(text); b.clicked.connect(slot); bar.addWidget(b)
        self.filter = QLineEdit(); self.filter.setPlaceholderText("Filtrer les lignes…")
        self.filter.textChanged.connect(self.apply_filter); bar.addWidget(self.filter, 1)

        self.summary = QLabel("Glissez un dossier ou des fichiers HH_Profile*.txt ici.")
        lay.addWidget(self.summary)
        self.table = QTableWidget(0, len(HEADERS))
        self.table.setHorizontalHeaderLabels(HEADERS)
        self.table.setSortingEnabled(True)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        h = self.table.horizontalHeader()
        h.setSectionResizeMode(0, QHeaderView.Stretch)
        for c in range(1, len(HEADERS)): h.setSectionResizeMode(c, QHeaderView.ResizeToContents)
        lay.addWidget(self.table, 1)
        note = QLabel("Chaque main compte pour chaque ligne utilisée (le total des lignes dépasse donc le total réel). "
                      "« dernière » = seulement la dernière ligne de la main.")
        note.setWordWrap(True); note.setStyleSheet("color: gray;")
        lay.addWidget(note)

    # ---- sources ----
    def start_dir(self):
        return self.settings.value("last_dir", "") or ""
    def open_folder(self):
        d = QFileDialog.getExistingDirectory(self, "Dossier Hand_Histories ou Profile", self.start_dir())
        if d: self.load([d])
    def open_files(self):
        fs, _ = QFileDialog.getOpenFileNames(self, "Fichiers HH_Profile", self.start_dir(), "Texte (*.txt)")
        if fs: self.load(fs)
    def reload(self):
        if self.sources: self.load(self.sources)
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dropEvent(self, e):
        self.load([u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()])

    def load(self, paths):
        self.sources = paths
        self.settings.setValue("last_dir", paths[0] if os.path.isdir(paths[0]) else os.path.dirname(paths[0]))
        files = core.find_files(paths)
        if not files:
            QMessageBox.information(self, "Rien trouvé", "Aucun fichier HH_Profile*.txt trouvé."); return
        stat, hands, net, no_note = core.analyze(files)
        self.rows = [(n, *v) for n, v in stat.items()]
        self.summary.setText("%d fichier(s), %d mains, résultat total %+.2f (%d mains sans ligne de profil)"
                             % (len(files), hands, net, no_note))
        self.fill()

    def fill(self):
        self.table.setSortingEnabled(False)
        self.table.setRowCount(len(self.rows))
        for r, (n, c, net, c2, net2) in enumerate(self.rows):
            items = [QTableWidgetItem(n), NumItem(c, str(c)), NumItem(net, "%+.2f" % net),
                     NumItem(net / c, "%+.2f" % (net / c)), NumItem(c2, str(c2)), NumItem(net2, "%+.2f" % net2)]
            for col, it in enumerate(items):
                if col in (2, 3, 5):
                    val = net if col != 5 else net2
                    it.setForeground(QBrush(QColor("#c0392b" if val < 0 else "#1e8449" if val > 0 else "#888")))
                self.table.setItem(r, col, it)
        self.table.setSortingEnabled(True)
        self.table.sortItems(2, Qt.AscendingOrder)   # plus grosses pertes en haut
        self.apply_filter()

    def apply_filter(self):
        t = self.filter.text().lower()
        for r in range(self.table.rowCount()):
            self.table.setRowHidden(r, bool(t) and t not in self.table.item(r, 0).text().lower())

    def export_csv(self):
        if not self.rows: return
        path, _ = QFileDialog.getSaveFileName(self, "Exporter", "profil_stats.csv", "CSV (*.csv)")
        if not path: return
        with open(path, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh, delimiter=";")
            w.writerow(["ligne", "mains", "net", "moyenne", "mains_derniere", "net_derniere"])
            for n, c, net, c2, net2 in sorted(self.rows, key=lambda r: r[2]):
                w.writerow([n, c, "%.2f" % net, "%.2f" % (net / c), c2, "%.2f" % net2])

if __name__ == "__main__":
    app = QApplication(sys.argv)
    w = Window(); w.show()
    if len(sys.argv) > 1: w.load(sys.argv[1:])
    sys.exit(app.exec())

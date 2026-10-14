"""Configuration d'une simulation Spin & Go (structure de blinds, temps, cagnottes)."""
import json
from dataclasses import dataclass, field, asdict

# Structure de blinds fournie (niveaux 1 à 10) + extension raisonnable au-delà.
DEFAULT_BLIND_LEVELS = [
    (10, 20), (15, 30), (20, 40), (30, 60), (40, 80),
    (50, 100), (60, 120), (80, 160), (100, 200), (150, 300),
    (200, 400), (300, 600), (400, 800), (500, 1000), (600, 1200),
    (800, 1600), (1000, 2000), (1500, 3000), (2000, 4000), (3000, 6000),
]

# Table des multiplicateurs : (multiplicateur, chances sur 1 000 000)
DEFAULT_MULTIPLIERS = [
    (1000, 5), (100, 100), (20, 500), (10, 5000),
    (5, 40000), (4, 130000), (3, 346200), (2, 478195),
]

# Répartition 1er / 2ème / 3ème de la cagnotte
DEFAULT_PAYOUT_SPLIT = (0.75, 0.15, 0.10)


@dataclass
class SimConfig:
    # --- format
    buyin: float = 1.0                    # € par joueur
    starting_stack: int = 500
    num_players: int = 3
    num_games: int = 100
    seed: int = 12345
    # --- blinds / temps
    blind_levels: list = field(default_factory=lambda: [list(x) for x in DEFAULT_BLIND_LEVELS])
    level_seconds: int = 90               # 60, 90 ou 120 au choix dans l'interface
    hand_base_seconds: float = 10.0       # durée simulée d'une main (hors actions)
    action_seconds: float = 3.0           # durée simulée par action
    showdown_seconds: float = 4.0         # supplément si showdown
    max_hands_per_game: int = 400
    # --- cagnotte
    multipliers: list = field(default_factory=lambda: [list(x) for x in DEFAULT_MULTIPLIERS])
    payout_split: list = field(default_factory=lambda: list(DEFAULT_PAYOUT_SPLIT))
    prize_pool_mode: str = "multiplier_x_buyin"   # ou "multiplier_x_total" (mult x 3 buy-ins)
    # --- profils : liste de {"name":..., "path":...}
    players: list = field(default_factory=list)
    rotate_seats: bool = True             # permute les places d'une partie à l'autre
    # --- calcul des symboles
    prwin_iterations: int = 200
    prwin_max_iterations: int = 300       # plafond si le profil demande plus (f$prwin_number_of_iterations)
    pt_db_path: str = ""                 # base de stats persistante (JSON) ; vide = stats de la partie seulement
    use_native_history_symbols: bool = True
    library_dir: str = ""                 # dossier de la bibliothèque OpenPPL (auto si vide)
    # --- sorties
    output_dir: str = "output"
    write_handhistory: bool = True
    write_player_files: bool = True
    workers: int = 0                      # 0 = auto (nombre de coeurs)

    def to_json(self):
        return json.dumps(asdict(self), indent=2, ensure_ascii=False)

    @staticmethod
    def from_dict(d):
        base = SimConfig()
        for k, v in d.items():
            if hasattr(base, k):
                setattr(base, k, v)
        base.validate()
        return base

    def validate(self):
        if self.num_players not in (2, 3):
            raise ValueError("Le format Spin & Go supporte 2 ou 3 joueurs")
        if self.level_seconds <= 0:
            raise ValueError("La durée d'un niveau doit être positive")
        if self.starting_stack <= 0:
            raise ValueError("Stack de départ invalide")
        if not self.blind_levels:
            raise ValueError("Aucun niveau de blinds")
        levels = [(int(a), int(b)) for a, b in self.blind_levels]
        for sb, bb in levels:
            if sb <= 0 or bb <= 0 or bb < sb:
                raise ValueError("Niveau de blinds invalide : %s/%s" % (sb, bb))
        self.blind_levels = [list(x) for x in levels]
        total = sum(int(c) for _, c in self.multipliers)
        if total <= 0:
            raise ValueError("Table de multiplicateurs vide")
        s = sum(self.payout_split)
        if abs(s - 1.0) > 1e-6:
            raise ValueError("La répartition des gains doit faire 100 %% (actuel : %.2f)" % (s * 100))
        if self.num_games < 1:
            raise ValueError("Nombre de parties invalide")
        return self

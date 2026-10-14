# Simulateur Spin & Go pour profils OpenHoldem

Simule des centaines de parties **Spin & Go** (3 joueurs, blinds montantes, buy-in 1 €, multiplicateur simulé)
entre des **profils OpenHoldem** (`.ohf` : OpenPPL ou OH-script). Aucun scraper, aucune table réelle : le moteur
de jeu, le langage de formules et les symboles sont réimplémentés en Python (bibliothèque standard uniquement, Python ≥ 3.8).

## Démarrage rapide

```bash
cd SpinGoSimulator
python3 server.py              # interface web : http://127.0.0.1:8765
python3 run_cli.py --games 300 --level-seconds 90 --out output \
   --player Nit=profiles/Tight_Nit.ohf --player LAG=profiles/Aggro_LAG.ohf --player Def=DefaultBot
python3 -m unittest discover -s tests      # tests
```

## Ce qui est simulé

| Élément | Détail |
|---|---|
| Format | NL Hold'em 3-max (ou 2 joueurs), 500 jetons chacun, pots annexes, relance courte non réouverte, heads-up |
| Blinds | 20 niveaux (les 10 premiers : 10/20 … 150/300), durée **1 min / 1 min 30 / 2 min** au choix |
| Horloge | simulée : secondes par main / action / showdown réglables (une partie = quelques minutes simulées, quelques ms réelles) |
| Cagnotte | multiplicateur tiré selon la table (×2 … ×1000, éditable) × buy-in, répartie 75 % / 15 % / 10 % (éditable) |
| Places | rotation aléatoire des sièges à chaque partie, graine reproductible |

## Langage et symboles OpenHoldem

* Analyseur fidèle à `CFormulaParser` : `WHEN/RETURN/FORCE/Set`, `RaiseTo/RaiseBy`, `Others`, priorités d'opérateurs, listes (`listXXX`, `islistXXX`), `hand$…`, `board$…`, `$AKs`, variables `user…`, symboles mémoire `me_st_/me_re_/me_inc_/me_add_/me_sub_`.
* Bibliothèque **OpenPPL** complète du dépôt (≈ 1 170 fonctions) chargée automatiquement, plus `DefaultBot` / Termita SNG.
* ≈ 370 symboles natifs recalculés depuis l'état exact de la partie : cartes, `pokerval`, `prwin` (Monte-Carlo), `handrank*`, positions, mises/pots, historique (`did*`, `prevaction`, `hi_*`), raisers/callers, tournoi, `ac_*`…
* Décision : OpenPPL (constantes d'action, chaîne de repli allin→betpot→betsize→raise→call→check→fold) ou OH-script (`f$allin`, `f$betpot_*`, `f$betsize`, `f$raise`, `f$call`, `f$check`, `f$fold`) ; `f$ini_function_*` et `f$prwin_*` pris en compte.
* Hors périmètre : PokerTracker (`pt_*` renvoient 0), DLL utilisateur, `mh_*` (bibliothèque externe à OpenHoldem).

## Sorties (`output/run_AAAAMMJJ_HHMMSS/`)

* `handhistory_global.txt` : toutes les mains, **cartes de tous les joueurs** (`Dealt to …` pour chacun)
* `players/<nom>_hands.txt|csv` : mains de chaque joueur (CSV : position, cartes, actions par tour, résultat…)
* `games.csv` (places, gains, multiplicateur), `player_stats.csv` / `summary.json` (VPIP, PFR, 3-bet, fold-to-3bet, steal, AF, c-bet, WTSD, W$SD, ITM, ROI, net €, bb/100…), `config.json`

## Interface web

Onglets : *Configuration* (blinds, durée des niveaux, multiplicateurs, profil par joueur avec import/vérification),
*Simulation* (progression + stats en direct), *Statistiques*, *Historique des mains*.

## Fichiers

`spingosim/` : `ohlang.py` (langage), `symbols.py` (symboles + décision), `engine.py` (jeu), `cards.py`, `payouts.py`,
`handhistory.py`, `stats.py`, `runner.py` (multiprocessing). `profiles/` : exemples OpenPPL.
`tests/` : tests unitaires + `coverage_check.py` (évalue toute la bibliothèque) + `demobots_check.py`.

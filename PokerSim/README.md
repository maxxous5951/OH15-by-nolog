# PokerSim (C++20)

Simulateur Hold'em No Limit moderne : **Cash**, **SNG** (2 à 10 joueurs, blinds montantes au temps ou aux mains)
et **Spin & Go** (multiplicateurs). Objectif : faire jouer entre eux des profils OpenHoldem
(`.ohf` OpenPPL / OH-script, `.oppl`, `.txt`) avec les symboles OpenHoldem recalculés nativement (dont `prwin`).

## Compiler

Windows (Visual Studio 2022, CMake intégré) :

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    build\Release\pokersim_tests.exe
    build\Release\pokersim.exe --format spin --players 3 --games 1000

Linux / macOS : `cmake -S . -B build -G Ninja && cmake --build build && ./build/pokersim_tests`

Aucune dépendance à installer (`nlohmann/json` est fourni dans `third_party/`).

## Utilisation

    pokersim --format spin --players 3 --games 3000 --level-seconds 90
    pokersim --format sng  --players 6 --games 500 --by hands --level-hands 8 --buyin 10 --stack 1500
    pokersim --format cash --players 6 --games 20 --hands 5000 --sb 5 --bb 10 --stack 1000 --rake 5 --rake-cap 30
    pokersim --config exemple.json
    pokersim ... --player Alice=builtin:tag --player Bob=builtin:call --hh historique.txt

Bots intégrés : `builtin:tag` (équité Monte-Carlo), `builtin:call`, `builtin:random`.

## Architecture

| Fichier | Rôle |
|---|---|
| `evaluator.hpp` | évaluateur 5-7 cartes (vérifié sur les 2 598 960 mains de 5 cartes) |
| `equity.*` | équité Monte-Carlo + énumération exacte |
| `engine.*` | moteur : mises, relances courtes, pots annexes, antes, rake, éliminations |
| `config.*` | formats Cash / SNG / Spin & Go, structures de blinds, gains |
| `brains.*` | interface `IBrain` (un profil OpenHoldem = un `IBrain`) + bots de test |
| `runner.*` | exécution multithread reproductible (graine par partie), statistiques |
| `ohlang.*` | langage de formules OpenHoldem : OH-script + OpenPPL (tokenizer, parseur, évaluation, listes de mains) |
| `shanky.cpp` | profils Shanky `.txt` / `.oPPL` ancien format -> `.ohf`, y compris les blocs `NEW LIST ... END LIST` |

## Feuille de route

1. **[fait]** moteur, trois formats, CLI, tests de conservation des jetons
2. **[fait]** Parseur de formules : `.ohf` (OpenPPL + OH-script), `.oppl`, `.txt` Shanky
3. Symboles OpenHoldem natifs (≈ 370 dans la version Python) et bibliothèque OpenPPL
4. `prwin` fidèle à OpenHoldem (adversaires pondérés, itérations demandées par le profil)
5. Historique PokerTracker simulé, exports, interface graphique

## Formules OpenHoldem (phase 2)

`ohdump` sérialise les fonctions d'un profil (diagnostic) :

    ohdump --summary profil.ohf       # fonctions, listes, erreurs de syntaxe
    ohdump --convert vieux_profil.txt # texte .ohf produit pour un profil Shanky

Validation : `tools/diff_test.sh build/ohdump <fichiers>` compare l'arbre de chaque fonction à `ohlang.py`.
Sur tous les profils et bibliothèques du dépôt : 6 418 fonctions, 6 414 identiques ; les 4 écarts sont les fonctions de `Rev023.oPPL` (Shanky) que le Python rejette et que `shanky.cpp` accepte (variables utilisateur `When ... User_X`, `In Cutoff`).

Choix assumés (fidèles à la source OpenHoldem, pas au Python) :
- opérateurs : division/modulo par zéro -> -1 (`kUndefined`), `unsigned long` 32 bits, `ln` brut ;
- listes de mains : poids 0-100 pris en compte (tirage `poids >= rand()%101`), plages `QQ-99`, `T7s-T3s`, `A2s+` ;
  `AK` sans `s`/`o` est une **erreur** (comme OpenHoldem) dans un `.ohf` ;
- profils Shanky : `AK` = `AKs AKo` (le format Shanky l'autorise), `KA` = `AK`.

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
3. **[fait]** Symboles natifs (698 noms testés) + bibliothèque OpenPPL + cerveau OpenHoldem : les profils jouent
4. **[fait]** `prwin` / `nhands` / `handrank` / `pokerval` portés depuis l'original (validés par oracle)
5. À faire : PokerTracker simulé (`pt_*`, actuellement -1), listes `vs$`, exports, interface graphique, performances

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

## Jouer avec des profils OpenHoldem (phase 3)

    pokersim --format spin --players 3 --games 500 --oh-root /chemin/vers/OH15-by-nolog \
      --player "Moi=DemoBots/OpenPPL_old_style_translated/No Limit/BSS/NL_6max_BSS_by_Subiendolo.txt" \
      --player "Gecko=DemoBots/OH-Script/No Limit/Gecko_NL_6Max.ohf" --player Call=builtin:call

`--oh-root` = dossier contenant `OpenPPL/OpenPPL_Library` (cherché automatiquement vers le haut sinon).
Bibliothèques chargées dans l'ordre d'OpenHoldem : OpenPPL_Library, DefaultBot, Gecko, Termita, Winngy, custom_function_library.
La CLI liste à la fin les **symboles inconnus** (évalués à 0) pour chaque profil.

`prwin` suit OpenHoldem : `f$prwin_number_of_iterations` du profil (50 000 par défaut via la bibliothèque Gecko !).
`--prwin-max N` plafonne ce nombre pour aller plus vite (0 = fidèle).

### Validation

| Test | Contre quoi | Résultat |
|---|---|---|
| `oracle_pokerval` | code **original** d'OpenHoldem (`CalculatePokerval` extrait + poker-eval) | 3 M tirages, 0 écart |
| `symbols_vs_python` | `symbols.py`, 698 symboles x états aléatoires | 1,47 M comparaisons, 0 désaccord inattendu |
| `prwin` | énumération exacte, Monte-Carlo indépendant, mutations | OK |

    cmake -S . -B build -DPOKERSIM_OH_ROOT=/chemin/vers/OH15-by-nolog && cmake --build build && ctest --test-dir build

### Écarts assumés par rapport à `symbols.py` (le C++ suit les sources d'OpenHoldem)

- `prwin`, `prtie`, `prlos` : algorithme de `CIteratorThread` (itérations demandées par le profil, adversaires = `f$prwin_number_of_opponents`
  borné à 1..22, pondération `willplay/wontplay/topclip/mustplay`, biais `(N-1)/N`). Le Python plafonnait à 20 000 itérations et à 9 adversaires.
- `prwinnow`, `prlosnow`, `nhands*` : énumération exacte des mains adverses (`CalculateNhands`). `handrank*` : tables d'OpenHoldem.
- `pokerval` d'une quinte flush vaut `INT_MIN` (conversion double -> int) : toutes les quintes flush sont **à égalité** dans `prwin`,
  et ne comptent pas dans `nouts` / `ncardsbetter`.
- `ante` : ante réelle de la table (le Python renvoyait 0).

### Limites connues

`pt_*` / `ptt_*` (PokerTracker) renvoient -1, `vs$*` renvoie 0, pas de DLL utilisateur ni de `prw1326` alimenté par DLL,
No Limit Hold'em uniquement.

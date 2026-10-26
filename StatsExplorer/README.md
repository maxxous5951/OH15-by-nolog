# OpenHoldem Stats Explorer

Programme C++ (Dear ImGui) pour explorer la base `oh_stats.db` créée par OpenHoldem
(voir `../StatsDatabase_README.md`) : liste des joueurs avec leurs stats, fiche joueur, liste des mains, replayer.

## Compiler (Windows / Linux / macOS)

Prérequis : CMake ≥ 3.14, un compilateur C++14, OpenGL. Dear ImGui 1.91.9 et GLFW 3.4 sont téléchargés par CMake.
Linux : paquets de développement X11 (`libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`).

```
cd StatsExplorer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build/StatsExplorer  chemin\vers\oh_stats.db      # Windows : build\Release\StatsExplorer.exe
```

Sans argument, le programme cherche `Hand_Histories/oh_stats.db` puis `oh_stats.db` dans le dossier courant.
Le dossier `fonts/` (police avec accents) est copié à côté de l'exécutable. Copies locales : `-DIMGUI_DIR=... -DGLFW_DIR=...`.

## Utilisation

- **Filtres** (barre du haut) : site, cash / tournois / les deux, période, position, mains minimum,
  « Last hands » (seulement les N dernières mains de chaque joueur, comme les symboles `pt_500hands_…`), recherche par nom.
- **Tableau des joueurs** (gauche) : un clic sur un en-tête trie, clic sur un joueur = fiche. `Columns...` choisit les
  colonnes parmi les 99 stats (préréglages General / Preflop / Postflop / Sizes, survol = symbole `pt_<stat>`).
- **Onglet Player** : toutes les stats du joueur par section (mêmes filtres).
- **Onglet Hands** : mains du joueur sélectionné (sinon celles du hero : cartes, position, résultat), 100 par page. Clic = ouvre la main.
- **Onglet Hand** : replayer (table, cartes, mises, pot ; boutons, curseur, lecture automatique) et texte de la main
  au format PokerStars (copiable).
- **File > Create demo database** : crée une base de mains aléatoires pour essayer (jamais sur un fichier existant).
- La base est rechargée toute seule quand OpenHoldem enregistre de nouvelles mains (View > Reload automatically).
  Le programme ne modifie jamais les mains ; la base est ouverte en WAL, OpenHoldem peut continuer à écrire.

## Tests

`./build_and_test.sh [dossier-imgui]` compile (g++/gcc seulement) et lance `tests/headless_render.cpp` : base de démo,
filtres, tris, chargement de 100 mains (conservation des jetons, replayer), puis l'interface rendue par un rasteriseur
logiciel (sans fenêtre) avec clics simulés ; les captures `.ppm` sont écrites dans le dossier de build.
Avec CMake : `ctest`. 537 vérifications.

## Base de données

Le schéma est passé à la version 2 (colonnes `uncalled`, `uncalled_seat`, `sb_seat`, `bb_seat`, `raise_by`) pour pouvoir
reconstruire les mains ; les bases version 1 sont migrées à l'ouverture (anciennes mains : sans la ligne « Uncalled bet »).

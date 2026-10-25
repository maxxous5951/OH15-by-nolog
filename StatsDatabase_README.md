# Base de stats OpenHoldem (alternative à PokerTracker)

Une base SQLite (un seul fichier, sans serveur) alimentée par les mains que le générateur de hand history reconstruit. Les stats sont servies via les **mêmes symboles `pt_…`** que PokerTracker : formules et advisor inchangés.

## Activation

1. Préférences → page **Stats Database** → source : *OpenHoldem database*.
2. (Optionnel) *Record hands* : enregistre les mains même si PokerTracker reste la source. Toujours actif si la base OpenHoldem est la source.
3. Page **Handhistory Generator** : choisir *Cash game* ou *Tournament* (le générateur de fichiers n'a pas besoin d'être activé).
4. Bouton **Test** : ouvre le fichier, vérifie le SQL de toutes les stats et affiche le nombre de mains.

Fichier par défaut : `Hand_Histories\oh_stats.db` dans le dossier OpenHoldem (champ vide = défaut). Par défaut PokerTracker reste la source et rien n'est enregistré.

| Préférence (registre / ini) | Rôle | Défaut |
|---|---|---|
| `stats_source` | 0 = PokerTracker, 1 = base OpenHoldem | 0 |
| `stats_db_record` | enregistrer les mains | 0 |
| `stats_db_by_position` | stats du joueur à sa position actuelle seulement | 0 |
| `stats_db_path` | chemin du fichier (vide = défaut) | vide |

## Syntaxe des symboles (la « commande »)

```
pt_<stat><siège>              pt_vpip3            VPIP du siège 3
pt_<stat>_<suffixe>           pt_pfr_raischair    PFR du joueur qui a relancé
pt_<N>hands_<stat><siège>     pt_500hands_vpip3   sur ses 500 dernières mains
pt_<N><période>_<stat><siège> pt_12months_3bet5   sur la période donnée
```

- **Siège** : 0 à 9. Les symboles avec suffixe (`_raischair`…) sont convertis en numéro de siège par OpenHoldem avant d'arriver ici ; je n'ai pas pu le tester en conditions réelles (pas de Windows), seuls les symboles à numéro de siège sont couverts par mes tests.
- **Suffixes** (cités par OpenHoldem) : `raischair`, `headsupchair`, `smallblindchair`, `bigblindchair`, `dealerchair`, `cutoffchair`, `userchair`, `firstraiserchair`, `firstcallerchair`, `lastcallerchair`.
- **Périodes** : `years`, `months`, `weeks`, `days`, `hours`, `minutes`, `seconds` (ex. `2days`). On ne peut pas mélanger nombre de mains et période.
- **Valeurs** : ratios entre 0 et 1 (ex. `0.25` = 25 %), `hands` est un compte. Une stat sans occasion (ex. `4bet` jamais observé) renvoie *indéfini* (`-1`), de même qu'un joueur inconnu.
- Les mains cash et tournoi sont séparées selon `istournament`, et par site (nom du tablemap).
- Pseudos : correspondance exacte (sans casse), sinon le pseudo le plus proche (1 faute pour 4–11 caractères, 2 au-delà) s'il est unique.

Exemple de formule : `when pt_vpip_raischair > 0.45 and pt_hands_raischair >= 30 ...`

## Stats disponibles (49)

Chaque stat = somme d'un compteur / somme d'un autre, sur les mains du joueur. Noms des compteurs = colonnes de `hand_player`.

### Général

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_hands<siège>` | Nombre de mains en base | `COUNT(*)` |
| `pt_vpip<siège>` | Mise volontairement de l'argent préflop (hors blinds) | `SUM(vpip) / COUNT(*)` |
| `pt_vpip_L<siège>` | Idem vpip (variante legacy) | `SUM(vpip) / COUNT(*)` |
| `pt_pfr<siège>` | Relance préflop | `SUM(pfr) / SUM(pfr_opp)` |
| `pt_pfr_L<siège>` | Idem pfr (variante legacy) | `SUM(pfr) / SUM(pfr_opp)` |
| `pt_rfi<siège>` | Relance en premier (pot non ouvert, sans limpers) | `SUM(rfi) / SUM(rfi_opp)` |
| `pt_wtsd<siège>` | Va au showdown (parmi les mains où il voit le flop) | `SUM(wtsd) / SUM(seen_flop)` |
| `pt_wssd<siège>` | Gagne de l'argent au showdown | `SUM(wssd) / SUM(wtsd)` |

### Préflop

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_steal_attempt<siège>` | Tentative de steal (open-raise depuis CO/BTN/SB) | `SUM(steal) / SUM(steal_opp)` |
| `pt_bb_fold_to_steal<siège>` | BB se couche face à un steal | `SUM(bb_fold_steal) / SUM(bb_steal_opp)` |
| `pt_sb_fold_to_steal<siège>` | SB se couche face à un steal | `SUM(sb_fold_steal) / SUM(sb_steal_opp)` |
| `pt_3bet_vs_steal<siège>` | 3-bet face à un steal | `SUM(three_bet_steal) / SUM(three_bet_steal_opp)` |
| `pt_bb_3bet_vs_steal<siège>` | BB 3-bet un steal | `SUM(bb_3bet_steal) / SUM(bb_3bet_steal_opp)` |
| `pt_sb_3bet_vs_steal<siège>` | SB 3-bet un steal | `SUM(sb_3bet_steal) / SUM(sb_3bet_steal_opp)` |
| `pt_preflop_called_raise<siège>` | Paye une relance préflop | `SUM(called_raise) / SUM(called_raise_opp)` |
| `pt_3bet<siège>` | 3-bet préflop | `SUM(three_bet) / SUM(three_bet_opp)` |
| `pt_fold_to_3bet<siège>` | Se couche face à un 3-bet préflop | `SUM(fold_3bet) / SUM(fold_3bet_opp)` |
| `pt_fold_to_3bet_afterR<siège>` | Se couche face à un 3-bet après avoir ouvert (ouvreur seulement) | `SUM(fold_3bet_r) / SUM(fold_3bet_r_opp)` |
| `pt_fold_to_resteal<siège>` | Se couche face à un 3-bet des blinds après son steal | `SUM(fold_resteal) / SUM(fold_resteal_opp)` |
| `pt_4bet<siège>` | 4-bet ou plus (relance un 3-bet ou plus) | `SUM(four_bet) / SUM(four_bet_opp)` |
| `pt_fold_to_4bet<siège>` | Se couche face à un 4-bet ou plus | `SUM(fold_4bet) / SUM(fold_4bet_opp)` |
| `pt_preflop_squeeze<siège>` | Squeeze (3-bet après une relance et au moins un caller) | `SUM(squeeze) / SUM(squeeze_opp)` |

### Agression

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_flop_af<siège>` | Facteur d'agression flop (mises+relances / calls) | `SUM(flop_br) / SUM(flop_call)` |
| `pt_turn_af<siège>` | Facteur d'agression turn | `SUM(turn_br) / SUM(turn_call)` |
| `pt_river_af<siège>` | Facteur d'agression river | `SUM(river_br) / SUM(river_call)` |
| `pt_total_af<siège>` | Facteur d'agression postflop total | `SUM(flop_br+turn_br+river_br) / SUM(flop_call+turn_call+river_call)` |
| `pt_flop_afq<siège>` | Fréquence d'agression flop (mises+relances / (mises+relances+calls+folds)) | `SUM(flop_br) / SUM(flop_br+flop_call+flop_fold)` |
| `pt_turn_afq<siège>` | Fréquence d'agression turn | `SUM(turn_br) / SUM(turn_br+turn_call+turn_fold)` |
| `pt_river_afq<siège>` | Fréquence d'agression river | `SUM(river_br) / SUM(river_br+river_call+river_fold)` |
| `pt_total_afq<siège>` | Fréquence d'agression postflop totale | `SUM(flop_br+turn_br+river_br) / SUM(flop_br+flop_call+flop_fold+turn_br+turn_call+turn_fold+river_br+river_call+river_fold)` |

### Rues vues

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_flop_seen<siège>` | Voit le flop | `SUM(seen_flop) / COUNT(*)` |
| `pt_turn_seen<siège>` | Voit le turn | `SUM(seen_turn) / COUNT(*)` |
| `pt_river_seen<siège>` | Voit la river | `SUM(seen_river) / COUNT(*)` |

### Continuation bets

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_flop_cbet<siège>` | Continuation bet flop | `SUM(flop_cbet) / SUM(flop_cbet_opp)` |
| `pt_flop_fold_to_cbet<siège>` | Se couche face à une c-bet flop | `SUM(flop_fold_cbet) / SUM(flop_cbet_faced)` |
| `pt_flop_raise_cbet<siège>` | Relance une c-bet flop | `SUM(flop_raise_cbet) / SUM(flop_cbet_faced)` |
| `pt_turn_cbet<siège>` | Continuation bet turn (après c-bet flop) | `SUM(turn_cbet) / SUM(turn_cbet_opp)` |
| `pt_turn_fold_to_cbet<siège>` | Se couche face à une c-bet turn | `SUM(turn_fold_cbet) / SUM(turn_cbet_faced)` |
| `pt_river_fold_to_cbet<siège>` | Se couche face à une c-bet river | `SUM(river_fold_cbet) / SUM(river_cbet_faced)` |

### Autres postflop

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_flop_checkraise<siège>` | Check-raise flop | `SUM(flop_cr) / SUM(flop_cr_opp)` |
| `pt_turn_checkraise<siège>` | Check-raise turn | `SUM(turn_cr) / SUM(turn_cr_opp)` |
| `pt_turn_checkcall<siège>` | Check-call turn | `SUM(turn_cc) / SUM(turn_cr_opp)` |
| `pt_flop_donkbet<siège>` | Donk bet flop (mise avant l'agresseur préflop) | `SUM(flop_donk) / SUM(flop_donk_opp)` |
| `pt_river_bet<siège>` | Mise à la river quand il peut ouvrir l'action | `SUM(river_bet) / SUM(river_bet_opp)` |

### Occasions (compteurs)

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_cbet_flop_opp<siège>` | Nb d'occasions de c-bet flop (compte) | `SUM(flop_cbet_opp)` |
| `pt_fold_to_3bet_afterR_opp<siège>` | Nb d'occasions fold-to-3bet après ouverture (compte) | `SUM(fold_3bet_r_opp)` |
| `pt_preflop_squeeze_opp<siège>` | Nb d'occasions de squeeze (compte) | `SUM(squeeze_opp)` |
| `pt_flop_fold_to_cbet__flop_raise_cbet_opp<siège>` | Nb de fois face à une c-bet flop (compte) | `SUM(flop_cbet_faced)` |
| `pt_flop_donkbet_opp<siège>` | Nb d'occasions de donk bet flop (compte) | `SUM(flop_donk_opp)` |

## Définitions

Alignées sur le [Statistical Reference Guide de PokerTracker](https://www.pokertracker.com/guides/PT3/general/statistical-reference-guide) :

- **VPIP** : call ou relance préflop, blinds non comptées. **PFR** : relance / occasions d'agir préflop.
- **3-bet / 4-bet+ / fold to 3-bet / fold to 4-bet** : comptés quelle que soit l'action précédente du joueur (l'ouvreur face à un 3-bet a une occasion de 4-bet), une fois par main.
- **Steal** : open-raise depuis CO, BTN ou SB, sans limpers. **Défense de blind** : seulement si personne n'a payé le steal. **Re-steal** : 3-bet d'une blind face à un steal.
- **C-bet** : miser en premier sur une rue quand on a fait la dernière relance sur la précédente ; turn/river seulement si les rues précédentes ont été c-bettées.
- **AF** = (mises+relances) / calls ; **AFq** = (mises+relances) / (mises+relances+calls+folds), checks exclus.
- **WTSD** = showdown / flop vu ; **W$SD** = gagne au showdown / va au showdown.
- **Positions** : EP (le reste), CO, BTN, SB, BB ; BTN prime sur CO, CO exclut les blinds (en heads-up le bouton est SB).

## Non couvert

27 stats PokerTracker n'existent pas encore dans cette base (le symbole renvoie *indéfini*) : `4bet_plus_ratio`, `call_f_float_bet`, `call_f_float_bet_in_3b_pot`, `cbet_flop_in_3bet_pot`, `cbet_flop_opp_in_3bet_pot`, `donk_flop_in_3bet_plus_pot`, `float_flop`, `float_flop_in_3bet_pot`, `flop_check_raise_in_3bet_plus_pot`, `flop_check_raise_in_3bet_pot`, `flop_float`, `flop_fold_to_3bet`, `fold_to_f_cbet_in_3bet_plus_pot_opp`, `fold_to_f_cbet_in_3bet_pot`, `fold_to_f_check_raise`, `fold_to_f_check_raise_3b_plus_pot`, `fold_to_f_check_raise_opp`, `fold_to_f_float_bet_in_3b_plus_pot`, `fold_to_f_float_bet_in_3b_pot`, `fold_to_pf_squeeze`, `fold_to_squeeze_as_caller`, `fold_to_squeeze_as_raiser`, `fold_to_squeeze_as_raiser_opp`, `icon`, `postflop_fold_to_3bet`, `river_fold_to_3bet`, `turn_fold_to_3bet`.

`icon` n'est pas supporté (désactivé aussi dans le code PokerTracker). Pas de filtres de limite, de taille de stack ni de type de tournoi.

## Structure de la base

- `hands` : une ligne par main (site, hand_id, date, table, blinds, board, pot, rake…). Clé unique `(site, hand_id)`, les doublons sont refusés.
- `hand_player` : une ligne par joueur et main (pseudo, siège, position, stacks, cartes, gains) + 70 compteurs.
- `hand_actions` : toutes les actions de la main.
- Version du schéma : `PRAGMA user_version` = 1. Une base créée par une version plus récente est refusée.
- Seules les mains qui passent les contrôles de cohérence du générateur sont enregistrées.

## Tests

```
StatsDbTests/build_and_run.sh      # Linux / macOS, nécessite g++ ; 222 vérifications
```

La partie Windows/MFC (préférences, branchement dans OpenHoldem) n'a pas été compilée dans l'environnement de test : compiler une fois sous Visual Studio.

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
- Les mains cash et tournoi sont séparées selon `istournament`, et par site (nom du tablemap). Si le joueur n'est pas trouvé avec ces deux critères, la recherche est élargie : tout type de partie, puis tout site (le niveau utilisé apparaît dans le log de débogage PokerTracker : `filter level 0/1/2`).
- Pseudos : correspondance exacte (sans casse), sinon le pseudo le plus proche (1 faute pour 4–11 caractères, 2 au-delà) s'il est unique.

Exemple de formule : `when pt_vpip_raischair > 0.45 and pt_hands_raischair >= 30 ...`

## Stats disponibles (99)

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

### BB vs SB (ouverture de la small blind)

La SB ouvre en premier (sans limpers) et la BB répond. Compte toutes les tailles ; les variantes par taille sont dans la section suivante.

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_bb_3bet_vs_sb<siège>` | BB 3-bet l'open-raise de la SB | `SUM(vs_sb_3bet) / SUM(vs_sb_opp)` |
| `pt_bb_fold_vs_sb<siège>` | BB se couche face à l'open-raise de la SB | `SUM(vs_sb_fold) / SUM(vs_sb_opp)` |
| `pt_bb_call_vs_sb<siège>` | BB paye l'open-raise de la SB | `SUM(vs_sb_call) / SUM(vs_sb_opp)` |


### Par taille d'ouverture (2bb / 2,5bb / 3bb)

Taille = *relance à* ÷ big blind : **2bb** = 1,75 à 2,25 · **2,5bb** = 2,25 à 2,75 · **3bb** = 2,75 à 3,5. Une ouverture hors de ces plages (min-raise, 4bb et plus) compte dans les stats globales (`rfi`, `3bet`…) mais dans aucune taille.

- **RFI / steal** : part des occasions d'ouvrir où le joueur a ouvert à cette taille. `rfi_2bb + rfi_25bb + rfi_3bb` ≤ `rfi`.
- **3-bet** : le joueur 3-bet **face à une ouverture de cette taille** (`3bet_vs_25bb` = 3-bet contre un open 2,5bb).

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_rfi_2bb<siège>` | RFI avec une ouverture à 2bb | `SUM(rfi_s2) / SUM(rfi_opp)` |
| `pt_rfi_25bb<siège>` | RFI avec une ouverture à 2,5bb | `SUM(rfi_s25) / SUM(rfi_opp)` |
| `pt_rfi_3bb<siège>` | RFI avec une ouverture à 3bb | `SUM(rfi_s3) / SUM(rfi_opp)` |
| `pt_steal_attempt_2bb<siège>` | Steal (CO/BTN/SB) à 2bb | `SUM(steal_s2) / SUM(steal_opp)` |
| `pt_steal_attempt_25bb<siège>` | Steal (CO/BTN/SB) à 2,5bb | `SUM(steal_s25) / SUM(steal_opp)` |
| `pt_steal_attempt_3bb<siège>` | Steal (CO/BTN/SB) à 3bb | `SUM(steal_s3) / SUM(steal_opp)` |
| `pt_3bet_vs_2bb<siège>` | 3-bet face à une ouverture à 2bb | `SUM(three_bet_s2) / SUM(three_bet_opp_s2)` |
| `pt_3bet_vs_25bb<siège>` | 3-bet face à une ouverture à 2,5bb | `SUM(three_bet_s25) / SUM(three_bet_opp_s25)` |
| `pt_3bet_vs_3bb<siège>` | 3-bet face à une ouverture à 3bb | `SUM(three_bet_s3) / SUM(three_bet_opp_s3)` |
| `pt_3bet_vs_steal_2bb<siège>` | 3-bet face à un steal à 2bb | `SUM(three_bet_steal_s2) / SUM(three_bet_steal_opp_s2)` |
| `pt_3bet_vs_steal_25bb<siège>` | 3-bet face à un steal à 2,5bb | `SUM(three_bet_steal_s25) / SUM(three_bet_steal_opp_s25)` |
| `pt_3bet_vs_steal_3bb<siège>` | 3-bet face à un steal à 3bb | `SUM(three_bet_steal_s3) / SUM(three_bet_steal_opp_s3)` |
| `pt_bb_3bet_vs_steal_2bb<siège>` | BB 3-bet un steal à 2bb | `SUM(bb_3bet_steal_s2) / SUM(bb_3bet_steal_opp_s2)` |
| `pt_bb_3bet_vs_steal_25bb<siège>` | BB 3-bet un steal à 2,5bb | `SUM(bb_3bet_steal_s25) / SUM(bb_3bet_steal_opp_s25)` |
| `pt_bb_3bet_vs_steal_3bb<siège>` | BB 3-bet un steal à 3bb | `SUM(bb_3bet_steal_s3) / SUM(bb_3bet_steal_opp_s3)` |
| `pt_sb_3bet_vs_steal_2bb<siège>` | SB 3-bet un steal à 2bb | `SUM(sb_3bet_steal_s2) / SUM(sb_3bet_steal_opp_s2)` |
| `pt_sb_3bet_vs_steal_25bb<siège>` | SB 3-bet un steal à 2,5bb | `SUM(sb_3bet_steal_s25) / SUM(sb_3bet_steal_opp_s25)` |
| `pt_sb_3bet_vs_steal_3bb<siège>` | SB 3-bet un steal à 3bb | `SUM(sb_3bet_steal_s3) / SUM(sb_3bet_steal_opp_s3)` |
| `pt_bb_3bet_vs_sb_2bb<siège>` | BB 3-bet l'open-raise de la SB à 2bb | `SUM(vs_sb_3bet_s2) / SUM(vs_sb_opp_s2)` |
| `pt_bb_3bet_vs_sb_25bb<siège>` | BB 3-bet l'open-raise de la SB à 2,5bb | `SUM(vs_sb_3bet_s25) / SUM(vs_sb_opp_s25)` |
| `pt_bb_3bet_vs_sb_3bb<siège>` | BB 3-bet l'open-raise de la SB à 3bb | `SUM(vs_sb_3bet_s3) / SUM(vs_sb_opp_s3)` |

Exemples : `pt_rfi_25bb4` (siège 4 ouvre-t-il à 2,5bb ?), `pt_bb_3bet_vs_sb_25bb2` (la BB du siège 2 3-bet-elle les opens 2,5bb de la SB ?), `pt_200hands_3bet_vs_3bb5`. Avec PokerTracker comme source ces symboles sont valides mais toujours indéfinis.

### Squeeze et 4-bet de l'ouvreur

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_fold_to_pf_squeeze<siège>` | Se couche face à un squeeze préflop (ouvreur ou caller) | `SUM(sq_def_fold_r+sq_def_fold_c) / SUM(sq_def_opp_r+sq_def_opp_c)` |
| `pt_fold_to_squeeze_as_raiser<siège>` | Idem, en tant qu'ouvreur initial | `SUM(sq_def_fold_r) / SUM(sq_def_opp_r)` |
| `pt_fold_to_squeeze_as_caller<siège>` | Idem, en tant que caller de l'ouverture | `SUM(sq_def_fold_c) / SUM(sq_def_opp_c)` |
| `pt_fold_to_squeeze_as_raiser_opp<siège>` | Nb de fois face à un squeeze en tant qu'ouvreur (compte) | `SUM(sq_def_opp_r)` |
| `pt_4bet_plus_ratio<siège>` | PFR × fréquence de 4-bet de l'ouvreur (définition PT4) | `SUM(pfr)*SUM(four_bet_r) / SUM(pfr_opp)*SUM(four_bet_r_opp)` |

Squeeze = relance après une ouverture et au moins un caller. Victimes = l'ouvreur et les joueurs qui ont payé avant le squeeze (les blinds qui n'avaient pas encore agi n'en font pas partie).

### Pots 3-bet (flop)

« Pot 3-bet+ » = au moins un 3-bet préflop (donc 3-bet, 4-bet…). « Pot 3-bet » = exactement un 3-bet. La définition est au niveau de la main, pas du joueur.

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_cbet_flop_in_3bet_pot<siège>` | C-bet flop en pot 3-bet+ | `SUM(flop_cbet_p3p) / SUM(flop_cbet_opp_p3p)` |
| `pt_cbet_flop_opp_in_3bet_pot<siège>` | Nb d'occasions de c-bet en pot 3-bet+ (compte) | `SUM(flop_cbet_opp_p3p)` |
| `pt_fold_to_f_cbet_in_3bet_pot<siège>` | Se couche face à une c-bet flop en pot 3-bet+ | `SUM(flop_fold_cbet_p3p) / SUM(flop_cbet_faced_p3p)` |
| `pt_fold_to_f_cbet_in_3bet_plus_pot_opp<siège>` | Nb de fois face à une c-bet en pot 3-bet+ (compte) | `SUM(flop_cbet_faced_p3p)` |
| `pt_donk_flop_in_3bet_plus_pot<siège>` | Donk bet flop en pot 3-bet+ | `SUM(flop_donk_p3p) / SUM(flop_donk_opp_p3p)` |
| `pt_flop_check_raise_in_3bet_pot<siège>` | Check-raise flop en pot 3-bet | `SUM(flop_cr_p3) / SUM(flop_cr_opp_p3)` |
| `pt_flop_check_raise_in_3bet_plus_pot<siège>` | Check-raise flop en pot 3-bet+ | `SUM(flop_cr_p3p) / SUM(flop_cr_opp_p3p)` |

### Check-raise subi, 3-bet postflop

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_fold_to_f_check_raise<siège>` | Se couche face à un check-raise flop après avoir misé | `SUM(flop_fcr) / SUM(flop_fcr_opp)` |
| `pt_fold_to_f_check_raise_3b_plus_pot<siège>` | Idem, en pot 3-bet+ | `SUM(flop_fcr_p3p) / SUM(flop_fcr_opp_p3p)` |
| `pt_fold_to_f_check_raise_opp<siège>` | Nb de fois face à un check-raise flop (compte) | `SUM(flop_fcr_opp)` |
| `pt_flop_fold_to_3bet<siège>` | Se couche face à un 3-bet au flop (mise, relance, re-relance) | `SUM(flop_f3) / SUM(flop_f3_opp)` |
| `pt_turn_fold_to_3bet<siège>` | Idem au turn | `SUM(turn_f3) / SUM(turn_f3_opp)` |
| `pt_river_fold_to_3bet<siège>` | Idem à la river | `SUM(river_f3) / SUM(river_f3_opp)` |
| `pt_postflop_fold_to_3bet<siège>` | Idem toutes rues postflop | `SUM(flop_f3+turn_f3+river_f3) / SUM(flop_f3_opp+turn_f3_opp+river_f3_opp)` |

Un 3-bet postflop = la 3ᵉ mise d'une rue (mise, relance, re-relance), quel que soit le joueur.

### Float

| Symbole | Signification | Calcul |
|---|---|---|
| `pt_float_flop<siège>` | Float flop : le caller (en position) mise quand l'agresseur préflop a checké, pot relancé une fois | `SUM(float_flop) / SUM(float_flop_opp)` |
| `pt_flop_float<siège>` | Identique à `float_flop` | `SUM(float_flop) / SUM(float_flop_opp)` |
| `pt_float_flop_in_3bet_pot<siège>` | Float en pot 3-bet : l'ouvreur qui a payé le 3-bet mise quand le 3-beteur checke | `SUM(float3_flop) / SUM(float3_flop_opp)` |
| `pt_call_f_float_bet<siège>` | L'agresseur préflop (pot relancé une fois) a checké le flop, face à une mise : paye | `SUM(fd_call_sr) / SUM(fd_opp_sr)` |
| `pt_call_f_float_bet_in_3b_pot<siège>` | Idem en pot 3-bet (agresseur = 3-beteur) : paye | `SUM(fd_call_p3) / SUM(fd_opp_p3)` |
| `pt_fold_to_f_float_bet_in_3b_pot<siège>` | Idem en pot 3-bet : se couche | `SUM(fd_fold_p3) / SUM(fd_opp_p3)` |
| `pt_fold_to_f_float_bet_in_3b_plus_pot<siège>` | Idem en pot 3-bet+ : se couche | `SUM(fd_fold_p3p) / SUM(fd_opp_p3p)` |

Le float est détecté sur le comportement (mise du caller après check de l'agresseur) ; la mise n'est pas vérifiée comme étant « bluff ». Les stats « face à un float bet » comptent toute mise qui suit un check de l'agresseur, pas seulement les floats.

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

Toutes les stats PokerTracker sont calculées **sauf `icon`** (icône d'auto-rate propriétaire de PokerTracker, désactivée aussi dans le code PT d'OpenHoldem) : son symbole renvoie *indéfini*. Pas de filtres de limite, de taille de stack ni de type de tournoi. Les tailles d'ouverture (2bb / 2,5bb / 3bb) existent seulement pour RFI, steal et 3-bet.

## Structure de la base

- `hands` : une ligne par main (site, hand_id, date, table, blinds, board, pot, rake…). Clé unique `(site, hand_id)`, les doublons sont refusés.
- `hand_player` : une ligne par joueur et main (pseudo, siège, position, stacks, cartes, gains) + 147 compteurs.
- `hand_actions` : toutes les actions de la main.
- Version du schéma : `PRAGMA user_version` = 1. Une base créée par une version plus récente est refusée.
- Seules les mains qui passent les contrôles de cohérence du générateur sont enregistrées.

## Tests

```
StatsDbTests/build_and_run.sh      # Linux / macOS, nécessite g++ ; 364 vérifications
```

La partie Windows/MFC (préférences, branchement dans OpenHoldem) n'a pas été compilée dans l'environnement de test : compiler une fois sous Visual Studio.

## Explorateur

Un programme graphique (Dear ImGui, C++) pour parcourir la base est dans `StatsExplorer/` (voir `StatsExplorer/README.md`).

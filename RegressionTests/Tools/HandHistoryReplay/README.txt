Replays the "snapshots" section of a rejected-hand log (rejected_<date>.txt
written by the handhistory generator) through the reconstructor of
OpenHoldem/HandHistoryCore.h and prints the resulting hands.
No OpenHoldem needed, standard C++ only:

  g++ -std=c++11 -I../../../OpenHoldem replay.cpp -o replay
  ./replay <log> t      (t = tournament, omit for cash game)

Expected for every log below: all hands "VALID".

folds_without_active_flag.txt
  Table where the "active" flag of a player who folded is never cleared:
  the folds have to be inferred from the stacks.

ddpoker_multiway_balance_lag.txt
  DDPoker, 5 players on the turn. The call of the last player is already in
  the pot, but not yet deducted from his stack, and another player (who folded)
  has exactly the same stack. Expected (DDPoker's own log): Antony folds,
  Trisana calls 33 and wins 140.

ddpoker_unseen_raises.txt
  DDPoker, three hands of a freeroll tournament (rejected_<date>.log, one block per
  rejected hand; the hero cards of the last hand were added by hand).
  * hand 1: the bet on the flop and the fold of the opponent happen between two
    heartbeats, the hand ends without a showdown (Savanah bets 12, Edie folds).
  * hand 2: a raise and the calls behind it on the flop are never seen
    (Tawanna bets 6, Edie raises to 30, Savanah folds, Tawanna calls).
  * hand 3: a 3-bet before the flop and a bet + call on the flop are never seen.
    Who bet first on the flop can not be known (the stacks are the same), the
    amounts and the pot are right.

ddpoker_headsup_instant_folds.txt
  Heads-up, very fast play with instant folds: the next hand starts (or ends)
  while the reconstructor still waits for the blinds. The snapshots seen in that
  time are buffered and replayed into the next hand, and the end stacks are taken
  from the first snapshot of the new hand, so no hand is lost or rejected.

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

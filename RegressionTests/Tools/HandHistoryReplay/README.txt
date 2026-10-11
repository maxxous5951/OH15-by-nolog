Replays the "snapshots" section of a rejected-hand log (rejected_<date>.log
written by the handhistory generator) through the reconstructor of
OpenHoldem/HandHistoryCore.h and prints the resulting hands.
No OpenHoldem needed, standard C++ only:

  g++ -std=c++11 -I../../../OpenHoldem replay.cpp -o replay
  ./replay folds_without_active_flag.log t     (t = tournament, omit for cash game)

Expected: "hand 1: VALID" and "hand 2: VALID".
folds_without_active_flag.log comes from a table where the "active" flag of a
player who folded is never cleared: the folds have to be inferred from the stacks.

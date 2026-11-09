# Style clone model: the rule, written before any test-set use (2026-11-06)

Split: by recording file, fixed in split/{train,val,test}.list. Everything is tuned on val. The test set is
touched once, by one final run, for the single chosen model and the replay settings as they stand.

One-step test: rows with the foe within 7 m, one query per 200 ms of play at most. Truth = what the player did
over the next 300 ms. Past = the 300 ms before. Bits W, S, A, D, guard R, guard L, each "held more than half".
Change score J = mean over the 6 bits of (caught - false), caught = predicted a change when one came,
false = predicted a change when none came. Key F1 = mean W,S,A,D. Guard F1 = mean R,L. Mouse = mean of the
turn and look correlations. Sharpness = RMS and p99 of predicted mouse over 300 ms, against the player's.
Compared on the same queries: model, replay (train set as its Db), repeat the last 300 ms, random train rows.

The model beats replay only if ALL of these hold on the test set:
1. J is higher than replay's and the 95% interval of the difference, bootstrapped by recording file, is above 0.
2. Key F1 and guard F1 are each no more than 0.02 below replay's.
3. Mouse correlation is no more than 0.03 below replay's.
4. Sharpness: model mouse RMS and p99 are each at least 70% of the player's.
5. Closed loop (simulated, foe from the test recordings): median facing error no more than 5 degrees worse than
   replay's, and idle share, key presses per second and mouse per second no further from the player's than replay's
   by more than 25% of the player's value.

If any one fails, replay stays the default and the report says which.

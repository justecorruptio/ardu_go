# Host-side tools

Both tools compile the real `ai.cpp`/`game.cpp` (Arduino headers
stubbed by `Arduboy2.h` + `avr/pgmspace.h` here), so they run exactly
the code the device runs. The Arduino build ignores this directory.

    make            # builds ./play, ./play_gui and ./harness

## play_gui — clickable macOS app

    ./play_gui

Native Cocoa window: click intersections to play, red ring marks the
last move. Buttons: Pass, Undo, New Game, Play as White, Save SGF
(writes the full game record to test/saved_games/ardugo_<timestamp>.sgf —
save whenever the AI does something wrong and hand over the file for
analysis). The title bar shows the AI's move, think time, and its
position/move win rates.

## play — interactive game in the terminal

    ./play          # you are Black (AI White, like the device's VS AI)
    ./play w        # you are White

Moves as coordinates (`e5`); commands `pass`, `undo`, `new`, `quit`.
Shows the AI's move time and its own win-rate estimate each move.
Host think time is far faster than the ATmega — judge move QUALITY
here, judge SPEED on the device.

## harness — automated strength testing vs GNU Go

    ./harness <games> <gnugo-level> [stats] [iterations] [reclaim]
    ./harness 10 0             # 10 games vs gnugo level 0
    ./harness 10 -1            # 10 games vs a random player (sanity: expect 10/10)
    ./harness 1 0 1            # one game with per-move root statistics
    ./harness 10 0 0 2000      # iteration-count override
    ./harness eval game_000.sgf  # per-move gnugo estimate vs raw playout eval
    ./harness hunt 40 0 6      # blunder hunt: 40 games at level 0; every AI
                               # move that drops >=6 pts in gnugo's estimate
                               # gets a forensic record (board, search tree,
                               # gnugo's preferred move) in hunt_report.txt
                               # plus a replay SGF per blunder

Writes one `game_NNN.sgf` per game into the working directory.
